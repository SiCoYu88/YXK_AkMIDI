#include "WwisePixelStreaming2Bridge.h"

#include "AkAudioDevice.h"
#include "AsyncInputSubsystem.h"
#include "AsyncInputRemoteBridge.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "PixelStreaming2Delegates.h"

DEFINE_LOG_CATEGORY_STATIC(LogWwisePixelStreaming2, Log, All);

namespace
{
	constexpr TCHAR ConfigSection[] = TEXT("WwisePixelStreaming2");
	constexpr TCHAR RemoteAnyKeySyntheticSourceId[] = TEXT("__WwisePixelStreamingAnyKey");
	constexpr uint32 RemoteAnyKeyTouchIndex = 0;

	bool TryPeekPixelStreamingKeyCode(FMemoryReader& Message, uint8& OutKeyCode)
	{
		const int64 OriginalOffset = Message.Tell();
		if (Message.TotalSize() - OriginalOffset < static_cast<int64>(sizeof(uint8)))
		{
			return false;
		}

		Message << OutKeyCode;
		Message.Seek(OriginalOffset);
		return !Message.IsError();
	}
}

FWwisePixelStreaming2Config FWwisePixelStreaming2Config::Load()
{
	FWwisePixelStreaming2Config Result;
	if (GConfig == nullptr)
	{
		return Result;
	}

	GConfig->GetBool(ConfigSection, TEXT("Enabled"), Result.bEnabled, GGameIni);
	GConfig->GetBool(ConfigSection, TEXT("ForwardRemoteInputToAsyncInput"), Result.bForwardRemoteInputToAsyncInput, GGameIni);
	GConfig->GetBool(ConfigSection, TEXT("ForwardRemoteAnyKeyToAsyncInput"), Result.bForwardRemoteAnyKeyToAsyncInput, GGameIni);
	GConfig->GetString(ConfigSection, TEXT("RemoteAnyKeyName"), Result.RemoteAnyKeyName, GGameIni);
	GConfig->GetString(ConfigSection, TEXT("StreamerId"), Result.StreamerId, GGameIni);
	FString OutputDeviceId;
	if (GConfig->GetString(ConfigSection, TEXT("OutputDeviceId"), OutputDeviceId, GGameIni))
	{
		Result.OutputDeviceId = FCString::Strtoui64(*OutputDeviceId, nullptr, 10);
	}
	GConfig->GetInt(ConfigSection, TEXT("QueueSlots"), Result.QueueSlots, GGameIni);
	GConfig->GetInt(ConfigSection, TEXT("MaxFrames"), Result.MaxFrames, GGameIni);
	GConfig->GetInt(ConfigSection, TEXT("MaxChannels"), Result.MaxChannels, GGameIni);
	GConfig->GetFloat(ConfigSection, TEXT("Gain"), Result.Gain, GGameIni);
	GConfig->GetFloat(ConfigSection, TEXT("StatusLogIntervalSeconds"), Result.StatusLogIntervalSeconds, GGameIni);
	GConfig->GetFloat(ConfigSection, TEXT("CaptureStallTimeoutSeconds"), Result.CaptureStallTimeoutSeconds, GGameIni);

	Result.QueueSlots = FMath::Clamp(Result.QueueSlots, 2, 64);
	Result.MaxFrames = FMath::Clamp(Result.MaxFrames, 64, 8192);
	Result.MaxChannels = FMath::Clamp(Result.MaxChannels, 1, 32);
	Result.Gain = FMath::Clamp(Result.Gain, 0.0f, 8.0f);
	Result.StatusLogIntervalSeconds = FMath::Clamp(Result.StatusLogIntervalSeconds, 0.0f, 60.0f);
	if (Result.RemoteAnyKeyName.IsEmpty())
	{
		Result.RemoteAnyKeyName = TEXT("PixelStreamingAnyKey");
	}
	if (Result.CaptureStallTimeoutSeconds > 0.0f)
	{
		Result.CaptureStallTimeoutSeconds = FMath::Clamp(Result.CaptureStallTimeoutSeconds, 1.0f, 60.0f);
	}
	return Result;
}

FWwisePixelStreaming2Bridge::FWwisePixelStreaming2Bridge(const FWwisePixelStreaming2Config& InConfig)
	: Config(InConfig)
	, Queue(MakeUnique<FWwiseAudioQueue>(InConfig.QueueSlots, InConfig.MaxFrames, InConfig.MaxChannels))
	, Producer(MakeShared<FWwisePixelStreaming2Producer>())
	, RegisteredOutputId(InConfig.OutputDeviceId == 0 ? AK_INVALID_OUTPUT_DEVICE_ID : static_cast<AkOutputDeviceID>(InConfig.OutputDeviceId))
{
	GainScratch.SetNumUninitialized(Config.MaxFrames * Config.MaxChannels);
}

FWwisePixelStreaming2Bridge::~FWwisePixelStreaming2Bridge()
{
	StopBridge();
}

bool FWwisePixelStreaming2Bridge::Start()
{
	if (WorkerThread != nullptr)
	{
		return true;
	}

	WorkEvent = FPlatformProcess::GetSynchEventFromPool(false);
	bRunWorker.store(true, std::memory_order_release);
	WorkerThread = FRunnableThread::Create(this, TEXT("WwisePixelStreaming2Worker"), 0, TPri_AboveNormal);
	if (WorkerThread == nullptr)
	{
		bRunWorker.store(false, std::memory_order_release);
		FPlatformProcess::ReturnSynchEventToPool(WorkEvent);
		WorkEvent = nullptr;
		return false;
	}

	TryInitialize();
	return true;
}

void FWwisePixelStreaming2Bridge::StopBridge()
{
	if (WorkerThread == nullptr && !bCaptureRegistered)
	{
		DetachStreamer();
		return;
	}

	bAcceptCallbacks.store(false, std::memory_order_release);
	const bool bCanUnregisterCapture = bCaptureRegistered
		&& WwiseDevice != nullptr
		&& FModuleManager::Get().IsModuleLoaded(TEXT("AkAudio"))
		&& FAkAudioDevice::IsInitialized();
	if (bCanUnregisterCapture)
	{
		const AKRESULT Result = WwiseDevice->UnregisterCaptureCallback(&CaptureCallback, RegisteredOutputId, this);
		if (Result != AK_Success)
		{
			if (IsEngineExitRequested())
			{
				UE_LOG(LogWwisePixelStreaming2, Verbose,
					TEXT("Wwise capture callback was already unavailable during engine exit (AKRESULT %d)."),
					static_cast<int32>(Result));
			}
			else
			{
				UE_LOG(LogWwisePixelStreaming2, Warning,
					TEXT("Failed to unregister Wwise capture callback (AKRESULT %d)."),
					static_cast<int32>(Result));
			}
		}
	}
	bCaptureRegistered = false;
	WwiseDevice = nullptr;

	while (ActiveCallbacks.load(std::memory_order_acquire) != 0)
	{
		FPlatformProcess::YieldThread();
	}

	bRunWorker.store(false, std::memory_order_release);
	if (WorkEvent != nullptr)
	{
		WorkEvent->Trigger();
	}
	if (WorkerThread != nullptr)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}
	if (WorkEvent != nullptr)
	{
		FPlatformProcess::ReturnSynchEventToPool(WorkEvent);
		WorkEvent = nullptr;
	}

	DetachStreamer();
}

void FWwisePixelStreaming2Bridge::TryInitialize()
{
	if (!Streamer.IsValid())
	{
		TryAttachStreamer();
	}
	if (!Streamer.IsValid())
	{
		return;
	}
	TryAttachRemoteInputBridge();
	if (!bCaptureRegistered)
	{
		TryRegisterWwiseCapture();
		return;
	}
	TryRecoverStalledCapture();
}

bool FWwisePixelStreaming2Bridge::RebindCapture()
{
	return RebindCaptureInternal(false);
}

FWwisePixelStreaming2Stats FWwisePixelStreaming2Bridge::GetStats() const
{
	FWwisePixelStreaming2Stats Result;
	Result.CapturedBuffers = CapturedBuffers.load(std::memory_order_relaxed);
	Result.PushedBuffers = PushedBuffers.load(std::memory_order_relaxed);
	Result.DroppedBuffers = DroppedBuffers.load(std::memory_order_relaxed);
	Result.RejectedBuffers = RejectedBuffers.load(std::memory_order_relaxed);
	Result.NonSilentBuffers = NonSilentBuffers.load(std::memory_order_relaxed);
	Result.CaptureRebinds = CaptureRebinds.load(std::memory_order_relaxed);
	Result.LastPeak = LastPeak.load(std::memory_order_relaxed);
	Result.MaxPeak = MaxPeak.load(std::memory_order_relaxed);
	Result.LastNumFrames = LastNumFrames.load(std::memory_order_relaxed);
	Result.LastNumChannels = LastNumChannels.load(std::memory_order_relaxed);
	Result.LastSampleRate = LastSampleRate.load(std::memory_order_relaxed);
	Result.bCaptureRegistered = bCaptureRegistered;
	Result.bStreamerAttached = Streamer.IsValid();
	const uint64 LastCycles = LastCaptureCycles.load(std::memory_order_relaxed);
	if (LastCycles != 0)
	{
		Result.SecondsSinceLastCapture = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastCycles);
	}
	return Result;
}

void FWwisePixelStreaming2Bridge::LogStatus() const
{
	const FWwisePixelStreaming2Stats Stats = GetStats();
	const IConsoleVariable* DisableTransmitAudio = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.WebRTC.DisableTransmitAudio"));
	const IConsoleVariable* WebRTCAudioGain = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.WebRTC.AudioGain"));
	const int32 bTransmitDisabled = DisableTransmitAudio != nullptr ? DisableTransmitAudio->GetInt() : -1;
	const float PixelStreamingGain = WebRTCAudioGain != nullptr ? WebRTCAudioGain->GetFloat() : -1.0f;
	const FString TargetId = Config.StreamerId.IsEmpty() && IPixelStreaming2Module::IsAvailable()
		? IPixelStreaming2Module::Get().GetDefaultStreamerID()
		: Config.StreamerId;

	UE_LOG(LogWwisePixelStreaming2, Display,
		TEXT("Status: CaptureRegistered=%s StreamerAttached=%s StreamerId='%s' OutputDeviceId=%llu "
			"Captured=%llu Pushed=%llu Dropped=%llu Rejected=%llu NonSilent=%llu Rebinds=%llu "
			"LastPeak=%.6f MaxPeak=%.6f LastCaptureAge=%.2fs Format=%d frames x %d channels @ %d Hz "
			"Gain=%.3f PS2DisableTransmitAudio=%d PS2AudioGain=%.3f"),
		Stats.bCaptureRegistered ? TEXT("true") : TEXT("false"),
		Stats.bStreamerAttached ? TEXT("true") : TEXT("false"),
		*TargetId,
		Config.OutputDeviceId,
		Stats.CapturedBuffers,
		Stats.PushedBuffers,
		Stats.DroppedBuffers,
		Stats.RejectedBuffers,
		Stats.NonSilentBuffers,
		Stats.CaptureRebinds,
		Stats.LastPeak,
		Stats.MaxPeak,
		Stats.SecondsSinceLastCapture,
		Stats.LastNumFrames,
		Stats.LastNumChannels,
		Stats.LastSampleRate,
		Config.Gain,
		bTransmitDisabled,
		PixelStreamingGain);
}

uint32 FWwisePixelStreaming2Bridge::Run()
{
	while (bRunWorker.load(std::memory_order_acquire))
	{
		WorkEvent->Wait(20);

		FWwiseAudioFrame Frame;
		while (bRunWorker.load(std::memory_order_relaxed) && Queue->TryPeek(Frame))
		{
			const int32 NumSamples = Frame.NumFrames * Frame.NumChannels;
			const float* DataToPush = Frame.Data;
			if (!FMath::IsNearlyEqual(Config.Gain, 1.0f))
			{
				FMemory::Memcpy(GainScratch.GetData(), Frame.Data, static_cast<SIZE_T>(NumSamples) * sizeof(float));
				ApplyGain(GainScratch.GetData(), NumSamples);
				DataToPush = GainScratch.GetData();
			}

			float Peak = 0.0f;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				Peak = FMath::Max(Peak, FMath::Abs(DataToPush[Index]));
			}
			LastPeak.store(Peak, std::memory_order_relaxed);
			float PreviousMax = MaxPeak.load(std::memory_order_relaxed);
			while (Peak > PreviousMax && !MaxPeak.compare_exchange_weak(PreviousMax, Peak, std::memory_order_relaxed))
			{
			}
			if (Peak > UE_SMALL_NUMBER)
			{
				NonSilentBuffers.fetch_add(1, std::memory_order_relaxed);
			}
			LastNumFrames.store(Frame.NumFrames, std::memory_order_relaxed);
			LastNumChannels.store(Frame.NumChannels, std::memory_order_relaxed);
			LastSampleRate.store(Frame.SampleRate, std::memory_order_relaxed);

			Producer->PushAudio(DataToPush, NumSamples, Frame.NumChannels, Frame.SampleRate);
			PushedBuffers.fetch_add(1, std::memory_order_relaxed);
			Queue->Pop();
		}
	}
	return 0;
}

void FWwisePixelStreaming2Bridge::Stop()
{
	bRunWorker.store(false, std::memory_order_release);
	if (WorkEvent != nullptr)
	{
		WorkEvent->Trigger();
	}
}

void FWwisePixelStreaming2Bridge::CaptureCallback(AkAudioBuffer& CaptureBuffer, AkOutputDeviceID OutputId, void* Cookie)
{
	FWwisePixelStreaming2Bridge* Bridge = static_cast<FWwisePixelStreaming2Bridge*>(Cookie);
	if (Bridge == nullptr)
	{
		return;
	}

	Bridge->ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
	if (Bridge->bAcceptCallbacks.load(std::memory_order_acquire))
	{
		Bridge->OnCapturedAudio(CaptureBuffer);
	}
	Bridge->ActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
}

void FWwisePixelStreaming2Bridge::OnCapturedAudio(AkAudioBuffer& CaptureBuffer)
{
	LastCaptureCycles.store(FPlatformTime::Cycles64(), std::memory_order_relaxed);
	const int32 NumFrames = static_cast<int32>(CaptureBuffer.uValidFrames);
	const int32 NumChannels = static_cast<int32>(CaptureBuffer.NumChannels());
	const float* Data = static_cast<const float*>(CaptureBuffer.GetInterleavedData());
	if (Data == nullptr || NumFrames <= 0 || NumFrames > Config.MaxFrames || NumChannels <= 0 || NumChannels > Config.MaxChannels)
	{
		RejectedBuffers.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	CapturedBuffers.fetch_add(1, std::memory_order_relaxed);
	if (!Queue->TryPush(Data, NumFrames, NumChannels, SampleRate))
	{
		DroppedBuffers.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	WorkEvent->Trigger();
}

bool FWwisePixelStreaming2Bridge::TryRegisterWwiseCapture()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AkAudio")))
	{
		FModuleManager::Get().LoadModule(TEXT("AkAudio"));
	}
	if (!FAkAudioDevice::IsInitialized())
	{
		if (!bLoggedWaitingForWwise)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Waiting for the Wwise SoundEngine to initialize."));
			bLoggedWaitingForWwise = true;
		}
		return false;
	}

	WwiseDevice = FAkAudioDevice::Get();
	if (WwiseDevice == nullptr)
	{
		return false;
	}

	SampleRate = static_cast<int32>(WwiseDevice->GetSampleRate());
	if (SampleRate <= 0)
	{
		SampleRate = 48000;
	}

	const AKRESULT Result = WwiseDevice->RegisterCaptureCallback(&CaptureCallback, RegisteredOutputId, this);
	if (Result != AK_Success)
	{
		if (LastCaptureRegistrationError != static_cast<int32>(Result))
		{
			UE_LOG(LogWwisePixelStreaming2, Error,
				TEXT("RegisterCaptureCallback failed for OutputDeviceId=%llu (AKRESULT %d)."),
				Config.OutputDeviceId,
				static_cast<int32>(Result));
			LastCaptureRegistrationError = static_cast<int32>(Result);
		}
		WwiseDevice = nullptr;
		return false;
	}

	bAcceptCallbacks.store(true, std::memory_order_release);
	bCaptureRegistered = true;
	LastCaptureCycles.store(FPlatformTime::Cycles64(), std::memory_order_relaxed);
	bLoggedWaitingForWwise = false;
	LastCaptureRegistrationError = INDEX_NONE;
	UE_LOG(LogWwisePixelStreaming2, Display,
		TEXT("Registered Wwise capture callback for OutputDeviceId=%llu at %d Hz."),
		Config.OutputDeviceId,
		SampleRate);
	return true;
}

bool FWwisePixelStreaming2Bridge::RebindCaptureInternal(bool bAutomatic)
{
	const TCHAR* Reason = bAutomatic ? TEXT("capture watchdog") : TEXT("console command");
	bAcceptCallbacks.store(false, std::memory_order_release);

	if (bCaptureRegistered && WwiseDevice != nullptr && FModuleManager::Get().IsModuleLoaded(TEXT("AkAudio")))
	{
		const AKRESULT UnregisterResult = WwiseDevice->UnregisterCaptureCallback(&CaptureCallback, RegisteredOutputId, this);
		if (UnregisterResult != AK_Success && UnregisterResult != AK_DeviceNotFound && UnregisterResult != AK_NotInitialized)
		{
			bAcceptCallbacks.store(true, std::memory_order_release);
			LastCaptureCycles.store(FPlatformTime::Cycles64(), std::memory_order_relaxed);
			UE_LOG(LogWwisePixelStreaming2, Error,
				TEXT("Capture rebind requested by %s, but unregister failed (AKRESULT %d)."),
				Reason,
				static_cast<int32>(UnregisterResult));
			return false;
		}
	}

	while (ActiveCallbacks.load(std::memory_order_acquire) != 0)
	{
		FPlatformProcess::YieldThread();
	}

	bCaptureRegistered = false;
	WwiseDevice = nullptr;
	if (!TryRegisterWwiseCapture())
	{
		UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Capture rebind requested by %s is waiting for Wwise output."), Reason);
		return false;
	}

	const uint64 RebindCount = CaptureRebinds.fetch_add(1, std::memory_order_relaxed) + 1;
	UE_LOG(LogWwisePixelStreaming2, Display,
		TEXT("Rebound Wwise capture callback to the current output (%s, count=%llu)."),
		Reason,
		RebindCount);
	return true;
}

void FWwisePixelStreaming2Bridge::TryRecoverStalledCapture()
{
	if (Config.CaptureStallTimeoutSeconds <= 0.0f)
	{
		return;
	}

	const uint64 LastCycles = LastCaptureCycles.load(std::memory_order_relaxed);
	if (LastCycles == 0)
	{
		return;
	}

	const double SecondsSinceLastCapture = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastCycles);
	if (SecondsSinceLastCapture < Config.CaptureStallTimeoutSeconds)
	{
		return;
	}

	UE_LOG(LogWwisePixelStreaming2, Warning,
		TEXT("No Wwise capture callback for %.2f seconds; rebinding the current output."),
		SecondsSinceLastCapture);
	RebindCaptureInternal(true);
}

bool FWwisePixelStreaming2Bridge::TryAttachStreamer()
{
	if (!IPixelStreaming2Module::IsAvailable())
	{
		if (!bLoggedWaitingForPixelStreaming)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Waiting for the PixelStreaming2 module."));
			bLoggedWaitingForPixelStreaming = true;
		}
		return false;
	}

	IPixelStreaming2Module& Module = IPixelStreaming2Module::Get();
	if (!Module.IsReady())
	{
		if (!bLoggedWaitingForPixelStreaming)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Waiting for PixelStreaming2 to become ready."));
			bLoggedWaitingForPixelStreaming = true;
		}
		return false;
	}
	bLoggedWaitingForPixelStreaming = false;

	const FString TargetId = Config.StreamerId.IsEmpty() ? Module.GetDefaultStreamerID() : Config.StreamerId;
	TSharedPtr<IPixelStreaming2Streamer> TargetStreamer = Module.FindStreamer(TargetId);
	if (!TargetStreamer)
	{
		if (!bLoggedWaitingForStreamer)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Waiting for Pixel Streaming 2 streamer '%s'."), *TargetId);
			bLoggedWaitingForStreamer = true;
		}
		return false;
	}

	TargetStreamer->AddAudioProducer(Producer);
	Streamer = TargetStreamer;
	AttachedStreamerId = TargetId;
	bLoggedWaitingForStreamer = false;
	UE_LOG(LogWwisePixelStreaming2, Display, TEXT("Attached Wwise audio producer to Pixel Streaming 2 streamer '%s'."), *TargetId);
	return true;
}

bool FWwisePixelStreaming2Bridge::TryAttachRemoteInputBridge()
{
	if (!Config.bForwardRemoteInputToAsyncInput || RemoteInputHandler.IsValid())
	{
		return true;
	}

	TSharedPtr<IPixelStreaming2Streamer> TargetStreamer = Streamer.Pin();
	if (!TargetStreamer)
	{
		return false;
	}
	UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get();
	if (Delegates == nullptr)
	{
		return false;
	}

	TSharedPtr<IPixelStreaming2InputHandler> InputHandler = TargetStreamer->GetInputHandler().Pin();
	if (!InputHandler)
	{
		if (!bLoggedWaitingForInputHandler)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Waiting for Pixel Streaming 2 input handler on streamer '%s'."), *AttachedStreamerId);
			bLoggedWaitingForInputHandler = true;
		}
		return false;
	}

	static const TCHAR* MessageTypes[] =
	{
		TEXT("KeyDown"),
		TEXT("KeyUp"),
		TEXT("MouseDown"),
		TEXT("MouseUp"),
	};

	for (const TCHAR* MessageType : MessageTypes)
	{
		IPixelStreaming2InputHandler::MessageHandlerFn OriginalHandler = InputHandler->FindMessageHandler(MessageType);
		if (!OriginalHandler)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Pixel Streaming 2 message handler '%s' is unavailable."), MessageType);
			continue;
		}

		OriginalInputHandlers.Add(MessageType, OriginalHandler);
		const FString StreamerId = AttachedStreamerId;
		const FString MessageTypeString(MessageType);
		InputHandler->RegisterMessageHandler(MessageType,
			[this, OriginalHandler = MoveTemp(OriginalHandler), StreamerId, MessageTypeString](FString SourceId, FMemoryReader Message) mutable
			{
				const bool bKeyDownMessage = MessageTypeString == TEXT("KeyDown");
				const bool bKeyUpMessage = MessageTypeString == TEXT("KeyUp");
				const FString RemoteSourceId = SourceId;
				uint8 KeyCode = 0;
				const bool bHasKeyCode = (bKeyDownMessage || bKeyUpMessage)
					&& TryPeekPixelStreamingKeyCode(Message, KeyCode);

				if (bKeyDownMessage && bHasKeyCode)
				{
					HandleRemoteKeyboardState_GameThread(RemoteSourceId, KeyCode, true);
				}

				FAsyncInputRemoteContextScope RemoteInputScope(StreamerId, SourceId);
				OriginalHandler(MoveTemp(SourceId), MoveTemp(Message));

				if (bKeyUpMessage && bHasKeyCode)
				{
					HandleRemoteKeyboardState_GameThread(RemoteSourceId, KeyCode, false);
				}
			});
	}

	if (OriginalInputHandlers.IsEmpty())
	{
		return false;
	}

	RemoteInputHandler = InputHandler;
	PixelStreamingDelegates = Delegates;
	bLoggedWaitingForInputHandler = false;
	if (!ClosedConnectionHandle.IsValid())
	{
		ClosedConnectionHandle = Delegates->OnClosedConnectionNative.AddRaw(
			this,
			&FWwisePixelStreaming2Bridge::HandleClosedConnection);
	}

	UE_LOG(LogWwisePixelStreaming2, Display,
		TEXT("Attached remote input forwarding for %d Pixel Streaming 2 message handlers on streamer '%s'."),
		OriginalInputHandlers.Num(),
		*AttachedStreamerId);
	return true;
}

void FWwisePixelStreaming2Bridge::DetachRemoteInputBridge()
{
	if (ClosedConnectionHandle.IsValid())
	{
		if (UPixelStreaming2Delegates* Delegates = PixelStreamingDelegates.Get())
		{
			Delegates->OnClosedConnectionNative.Remove(ClosedConnectionHandle);
		}
		ClosedConnectionHandle.Reset();
	}
	PixelStreamingDelegates.Reset();

	ReleaseAllRemoteKeyboardState_GameThread();

	if (TSharedPtr<IPixelStreaming2InputHandler> InputHandler = RemoteInputHandler.Pin())
	{
		for (const TPair<FString, IPixelStreaming2InputHandler::MessageHandlerFn>& Pair : OriginalInputHandlers)
		{
			InputHandler->RegisterMessageHandler(Pair.Key, Pair.Value);
		}
	}

	if (!AttachedStreamerId.IsEmpty())
	{
		FAsyncInputRemoteBridge::NotifyStreamerDisconnected(AttachedStreamerId);
	}
	OriginalInputHandlers.Empty();
	RemoteInputHandler.Reset();
	bLoggedWaitingForInputHandler = false;
}

void FWwisePixelStreaming2Bridge::HandleClosedConnection(FString StreamerId, FString PlayerId)
{
	if (Config.bForwardRemoteInputToAsyncInput && StreamerId == AttachedStreamerId)
	{
		ReleaseRemoteKeyboardStateForSource_GameThread(PlayerId);
		FAsyncInputRemoteBridge::NotifySessionDisconnected(StreamerId, PlayerId);
	}
}

void FWwisePixelStreaming2Bridge::HandleRemoteKeyboardState_GameThread(const FString& SourceId, uint8 KeyCode, bool bPressed)
{
	if (!Config.bForwardRemoteInputToAsyncInput
		|| !Config.bForwardRemoteAnyKeyToAsyncInput
		|| Config.RemoteAnyKeyName.IsEmpty()
		|| AttachedStreamerId.IsEmpty())
	{
		return;
	}

	if (!ensureMsgf(IsInGameThread(), TEXT("Remote keyboard aggregate state must be updated on the GameThread.")))
	{
		return;
	}

	if (bPressed)
	{
		RemotePressedKeyCodesBySource.FindOrAdd(SourceId).Add(KeyCode);
	}
	else if (TSet<uint8>* PressedKeys = RemotePressedKeyCodesBySource.Find(SourceId))
	{
		PressedKeys->Remove(KeyCode);
		if (PressedKeys->IsEmpty())
		{
			RemotePressedKeyCodesBySource.Remove(SourceId);
		}
	}

	const int32 PressedKeyCount = GetRemotePressedKeyCount();
	if (PressedKeyCount > 0 && !bRemoteAnyKeyPressed)
	{
		bRemoteAnyKeyPressed = true;
		SendRemoteAnyKeyEvent_GameThread(true);
	}
	else if (PressedKeyCount == 0 && bRemoteAnyKeyPressed)
	{
		bRemoteAnyKeyPressed = false;
		SendRemoteAnyKeyEvent_GameThread(false);
	}
}

void FWwisePixelStreaming2Bridge::ReleaseRemoteKeyboardStateForSource_GameThread(const FString& SourceId)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Remote keyboard aggregate state must be released on the GameThread.")))
	{
		return;
	}

	if (!RemotePressedKeyCodesBySource.Remove(SourceId))
	{
		return;
	}

	if (GetRemotePressedKeyCount() == 0 && bRemoteAnyKeyPressed)
	{
		bRemoteAnyKeyPressed = false;
		SendRemoteAnyKeyEvent_GameThread(false);
	}
}

void FWwisePixelStreaming2Bridge::ReleaseAllRemoteKeyboardState_GameThread()
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Remote keyboard aggregate state must be released on the GameThread.")))
	{
		RemotePressedKeyCodesBySource.Empty();
		bRemoteAnyKeyPressed = false;
		return;
	}

	RemotePressedKeyCodesBySource.Empty();
	if (bRemoteAnyKeyPressed)
	{
		bRemoteAnyKeyPressed = false;
		SendRemoteAnyKeyEvent_GameThread(false);
	}
}

void FWwisePixelStreaming2Bridge::SendRemoteAnyKeyEvent_GameThread(bool bPressed)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Remote any-key event must be sent on the GameThread.")))
	{
		return;
	}

	UAsyncInputSubsystem* AsyncInputSubsystem = UAsyncInputSubsystem::Get();
	if (AsyncInputSubsystem == nullptr)
	{
		return;
	}

	FAsyncInputTouchEvent AnyKeyEvent(
		RemoteAnyKeyTouchIndex,
		Config.RemoteAnyKeyName,
		bPressed ? EAsyncInputTouchType::KeyStart : EAsyncInputTouchType::KeyEnd);
	AnyKeyEvent.SetRemoteSource(AttachedStreamerId, RemoteAnyKeySyntheticSourceId);
	AsyncInputSubsystem->NotifyAsyncInputEvent_GameThread(AnyKeyEvent);
}

int32 FWwisePixelStreaming2Bridge::GetRemotePressedKeyCount() const
{
	int32 Count = 0;
	for (const TPair<FString, TSet<uint8>>& Pair : RemotePressedKeyCodesBySource)
	{
		Count += Pair.Value.Num();
	}
	return Count;
}

void FWwisePixelStreaming2Bridge::DetachStreamer()
{
	DetachRemoteInputBridge();
	if (TSharedPtr<IPixelStreaming2Streamer> TargetStreamer = Streamer.Pin())
	{
		TargetStreamer->RemoveAudioProducer(Producer);
	}
	Streamer.Reset();
	AttachedStreamerId.Reset();
}

void FWwisePixelStreaming2Bridge::ApplyGain(float* Data, int32 NumSamples) const
{
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		Data[Index] *= Config.Gain;
	}
}
