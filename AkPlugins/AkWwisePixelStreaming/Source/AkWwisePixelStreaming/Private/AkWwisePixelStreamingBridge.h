#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "IPixelStreaming2AudioProducer.h"
#include "AK/SoundEngine/Common/AkCallbackTypes.h"

#include <atomic>

#include "AkWwisePixelStreamingModule.h"
#include "AkWwiseAudioQueue.h"

class FAkAudioDevice;
class IPixelStreaming2Streamer;

class FAkWwisePixelStreamingProducer final : public IPixelStreaming2AudioProducer
{
};

struct FAkWwisePixelStreamingConfig
{
	bool bEnabled = true;
	FString StreamerId;
	uint64 OutputDeviceId = 0;
	int32 QueueSlots = 8;
	int32 MaxFrames = 2048;
	int32 MaxChannels = 16;
	float Gain = 1.0f;

	static FAkWwisePixelStreamingConfig Load();
};

class FAkWwisePixelStreamingBridge final : public FRunnable
{
public:
	explicit FAkWwisePixelStreamingBridge(const FAkWwisePixelStreamingConfig& InConfig);
	virtual ~FAkWwisePixelStreamingBridge() override;

	bool Start();
	void StopBridge();
	void TryInitialize();
	FAkWwisePixelStreamingStats GetStats() const;

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	static void CaptureCallback(class AkAudioBuffer& CaptureBuffer, AkOutputDeviceID OutputId, void* Cookie);
	void OnCapturedAudio(class AkAudioBuffer& CaptureBuffer);
	bool TryRegisterWwiseCapture();
	bool TryAttachStreamer();
	void DetachStreamer();
	void ApplyGain(float* Data, int32 NumSamples) const;

	FAkWwisePixelStreamingConfig Config;
	TUniquePtr<FAkWwiseAudioQueue> Queue;
	TSharedPtr<FAkWwisePixelStreamingProducer> Producer;
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
