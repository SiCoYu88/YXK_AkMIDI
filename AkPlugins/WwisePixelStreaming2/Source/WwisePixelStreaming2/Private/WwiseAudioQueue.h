#pragma once

#include "CoreMinimal.h"

#include <atomic>

struct FWwiseAudioFrame
{
	const float* Data = nullptr;
	int32 NumFrames = 0;
	int32 NumChannels = 0;
	int32 SampleRate = 0;
};

class FWwiseAudioQueue
{
public:
	FWwiseAudioQueue(uint32 InCapacity, int32 InMaxFrames, int32 InMaxChannels);

	bool TryPush(const float* Data, int32 NumFrames, int32 NumChannels, int32 SampleRate);
	bool TryPeek(FWwiseAudioFrame& OutFrame) const;
	void Pop();

	uint32 GetCapacity() const { return Capacity; }
	int32 GetMaxFrames() const { return MaxFrames; }
	int32 GetMaxChannels() const { return MaxChannels; }

private:
	struct FSlot
	{
		TUniquePtr<float[]> Data;
		int32 NumFrames = 0;
		int32 NumChannels = 0;
		int32 SampleRate = 0;
	};

	TArray<FSlot> Slots;
	uint32 Capacity = 0;
	int32 MaxFrames = 0;
	int32 MaxChannels = 0;
	std::atomic<uint32> WriteIndex { 0 };
	std::atomic<uint32> ReadIndex { 0 };
};
