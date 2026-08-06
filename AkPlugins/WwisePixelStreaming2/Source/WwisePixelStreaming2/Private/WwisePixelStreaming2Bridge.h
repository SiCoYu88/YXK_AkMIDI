#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "IPixelStreaming2AudioProducer.h"
#include "IPixelStreaming2InputHandler.h"
#include "UObject/WeakObjectPtr.h"
#include "AK/SoundEngine/Common/AkCallbackTypes.h"

#include <atomic>

#include "WwisePixelStreaming2Module.h"
#include "WwiseAudioQueue.h"

class FAkAudioDevice;
class IPixelStreaming2Streamer;
class UPixelStreaming2Delegates;

class FWwisePixelStreaming2Producer final : public IPixelStreaming2AudioProducer
{
};

struct FWwisePixelStreaming2Config
{
	bool bEnabled = true;
	bool bForwardRemoteInputToAsyncInput = true;
	FString StreamerId;
	uint64 OutputDeviceId = 0;
	int32 QueueSlots = 8;
	int32 MaxFrames = 2048;
	int32 MaxChannels = 16;
	float Gain = 1.0f;
	float StatusLogIntervalSeconds = 5.0f;
	float CaptureStallTimeoutSeconds = 2.0f;

	static FWwisePixelStreaming2Config Load();
};

class FWwisePixelStreaming2Bridge final : public FRunnable
{
public:
	explicit FWwisePixelStreaming2Bridge(const FWwisePixelStreaming2Config& InConfig);
	virtual ~FWwisePixelStreaming2Bridge() override;

	bool Start();
	void StopBridge();
	void TryInitialize();
	bool RebindCapture();
	FWwisePixelStreaming2Stats GetStats() const;
	void LogStatus() const;
	float GetStatusLogIntervalSeconds() const { return Config.StatusLogIntervalSeconds; }

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	static void CaptureCallback(class AkAudioBuffer& CaptureBuffer, AkOutputDeviceID OutputId, void* Cookie);
	void OnCapturedAudio(class AkAudioBuffer& CaptureBuffer);
	bool TryRegisterWwiseCapture();
	bool RebindCaptureInternal(bool bAutomatic);
	void TryRecoverStalledCapture();
	bool TryAttachStreamer();
	bool TryAttachRemoteInputBridge();
	void DetachRemoteInputBridge();
	void HandleClosedConnection(FString StreamerId, FString PlayerId);
	void DetachStreamer();
	void ApplyGain(float* Data, int32 NumSamples) const;

	FWwisePixelStreaming2Config Config;
	TUniquePtr<FWwiseAudioQueue> Queue;
	TSharedPtr<FWwisePixelStreaming2Producer> Producer;
	TWeakPtr<IPixelStreaming2Streamer> Streamer;
	TWeakPtr<IPixelStreaming2InputHandler> RemoteInputHandler;
	TWeakObjectPtr<UPixelStreaming2Delegates> PixelStreamingDelegates;
	TMap<FString, IPixelStreaming2InputHandler::MessageHandlerFn> OriginalInputHandlers;
	FString AttachedStreamerId;
	FDelegateHandle ClosedConnectionHandle;
	FAkAudioDevice* WwiseDevice = nullptr;
	class FRunnableThread* WorkerThread = nullptr;
	class FEvent* WorkEvent = nullptr;
	AkOutputDeviceID RegisteredOutputId;
	int32 SampleRate = 48000;
	TArray<float> GainScratch;

	std::atomic<bool> bAcceptCallbacks { false };
	std::atomic<bool> bRunWorker { false };
	std::atomic<int32> ActiveCallbacks { 0 };
	std::atomic<uint64> CapturedBuffers { 0 };
	std::atomic<uint64> PushedBuffers { 0 };
	std::atomic<uint64> DroppedBuffers { 0 };
	std::atomic<uint64> RejectedBuffers { 0 };
	std::atomic<uint64> NonSilentBuffers { 0 };
	std::atomic<uint64> CaptureRebinds { 0 };
	std::atomic<uint64> LastCaptureCycles { 0 };
	std::atomic<float> LastPeak { 0.0f };
	std::atomic<float> MaxPeak { 0.0f };
	std::atomic<int32> LastNumFrames { 0 };
	std::atomic<int32> LastNumChannels { 0 };
	std::atomic<int32> LastSampleRate { 0 };
	bool bCaptureRegistered = false;
	bool bLoggedWaitingForWwise = false;
	bool bLoggedWaitingForPixelStreaming = false;
	bool bLoggedWaitingForStreamer = false;
	bool bLoggedWaitingForInputHandler = false;
	int32 LastCaptureRegistrationError = INDEX_NONE;
};
