#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "IPixelStreaming2AudioProducer.h"
#include "AK/SoundEngine/Common/AkCallbackTypes.h"

#include <atomic>

#include "WwisePixelStreaming2Module.h"
#include "WwiseAudioQueue.h"

class FAkAudioDevice;
class IPixelStreaming2Streamer;

class FWwisePixelStreaming2Producer final : public IPixelStreaming2AudioProducer
{
};

struct FWwisePixelStreaming2Config
{
	bool bEnabled = true;
	FString StreamerId;
	uint64 OutputDeviceId = 0;
	int32 QueueSlots = 8;
	int32 MaxFrames = 2048;
	int32 MaxChannels = 16;
	float Gain = 1.0f;

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
	FWwisePixelStreaming2Stats GetStats() const;

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	static void CaptureCallback(class AkAudioBuffer& CaptureBuffer, AkOutputDeviceID OutputId, void* Cookie);
	void OnCapturedAudio(class AkAudioBuffer& CaptureBuffer);
	bool TryRegisterWwiseCapture();
	bool TryAttachStreamer();
	void DetachStreamer();
	void ApplyGain(float* Data, int32 NumSamples) const;

	FWwisePixelStreaming2Config Config;
	TUniquePtr<FWwiseAudioQueue> Queue;
	TSharedPtr<FWwisePixelStreaming2Producer> Producer;
	TWeakPtr<IPixelStreaming2Streamer> Streamer;
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
	bool bCaptureRegistered = false;
};
