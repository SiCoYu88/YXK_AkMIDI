#include "WwisePixelStreaming2Bridge.h"

#include "AkAudioDevice.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWwisePixelStreaming2, Log, All);

namespace
{
	constexpr TCHAR ConfigSection[] = TEXT("WwisePixelStreaming2");
}

FWwisePixelStreaming2Config FWwisePixelStreaming2Config::Load()
{
	FWwisePixelStreaming2Config Result;
	if (GConfig == nullptr)
	{
		return Result;
	}

	GConfig->GetBool(ConfigSection, TEXT("Enabled"), Result.bEnabled, GGameIni);
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

	Result.QueueSlots = FMath::Clamp(Result.QueueSlots, 2, 64);
	Result.MaxFrames = FMath::Clamp(Result.MaxFrames, 64, 8192);
	Result.MaxChannels = FMath::Clamp(Result.MaxChannels, 1, 32);
	Result.Gain = FMath::Clamp(Result.Gain, 0.0f, 8.0f);
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
	if (bCaptureRegistered && WwiseDevice != nullptr && FModuleManager::Get().IsModuleLoaded(TEXT("AkAudio")))
	{
		const AKRESULT Result = WwiseDevice->UnregisterCaptureCallback(&CaptureCallback, RegisteredOutputId, this);
		if (Result != AK_Success)
		{
			UE_LOG(LogWwisePixelStreaming2, Warning, TEXT("Failed to unregister Wwise capture callback (AKRESULT %d)."), static_cast<int32>(Result));
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
	if (!bCaptureRegistered)
	{
		TryRegisterWwiseCapture();
	}
	if (!Streamer.IsValid())
	{
		TryAttachStreamer();
	}
}

FWwisePixelStreaming2Stats FWwisePixelStreaming2Bridge::GetStats() const
{
	FWwisePixelStreaming2Stats Result;
	Result.CapturedBuffers = CapturedBuffers.load(std::memory_order_relaxed);
	Result.PushedBuffers = PushedBuffers.load(std::memory_order_relaxed);
	Result.DroppedBuffers = DroppedBuffers.load(std::memory_order_relaxed);
	Result.RejectedBuffers = RejectedBuffers.load(std::memory_order_relaxed);
	return Result;
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
		WwiseDevice = nullptr;
		return false;
	}

	bAcceptCallbacks.store(true, std::memory_order_release);
	bCaptureRegistered = true;
	UE_LOG(LogWwisePixelStreaming2, Log, TEXT("Capturing Wwise output at %d Hz."), SampleRate);
	return true;
}

bool FWwisePixelStreaming2Bridge::TryAttachStreamer()
{
	if (!IPixelStreaming2Module::IsAvailable())
	{
		return false;
	}

	IPixelStreaming2Module& Module = IPixelStreaming2Module::Get();
	if (!Module.IsReady())
	{
		return false;
	}

	const FString TargetId = Config.StreamerId.IsEmpty() ? Module.GetDefaultStreamerID() : Config.StreamerId;
	TSharedPtr<IPixelStreaming2Streamer> TargetStreamer = Module.FindStreamer(TargetId);
	if (!TargetStreamer)
	{
		return false;
	}

	TargetStreamer->AddAudioProducer(Producer);
	Streamer = TargetStreamer;
	UE_LOG(LogWwisePixelStreaming2, Log, TEXT("Attached Wwise audio producer to Pixel Streaming 2 streamer '%s'."), *TargetId);
	return true;
}

void FWwisePixelStreaming2Bridge::DetachStreamer()
{
	if (TSharedPtr<IPixelStreaming2Streamer> TargetStreamer = Streamer.Pin())
	{
		TargetStreamer->RemoveAudioProducer(Producer);
	}
	Streamer.Reset();
}

void FWwisePixelStreaming2Bridge::ApplyGain(float* Data, int32 NumSamples) const
{
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		Data[Index] *= Config.Gain;
	}
}
