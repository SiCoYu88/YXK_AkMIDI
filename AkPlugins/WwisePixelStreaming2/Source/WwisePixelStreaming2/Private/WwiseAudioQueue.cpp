#include "WwiseAudioQueue.h"

#include "HAL/UnrealMemory.h"

FWwiseAudioQueue::FWwiseAudioQueue(uint32 InCapacity, int32 InMaxFrames, int32 InMaxChannels)
	: Capacity(FMath::Max(InCapacity, 2u))
	, MaxFrames(FMath::Max(InMaxFrames, 1))
	, MaxChannels(FMath::Max(InMaxChannels, 1))
{
	Slots.SetNum(Capacity);
	const int32 SamplesPerSlot = MaxFrames * MaxChannels;
	for (FSlot& Slot : Slots)
	{
		Slot.Data = MakeUnique<float[]>(SamplesPerSlot);
	}
}

bool FWwiseAudioQueue::TryPush(const float* Data, int32 NumFrames, int32 NumChannels, int32 SampleRate)
{
	if (Data == nullptr || NumFrames <= 0 || NumFrames > MaxFrames || NumChannels <= 0 || NumChannels > MaxChannels || SampleRate <= 0)
	{
		return false;
	}

	const uint32 CurrentWrite = WriteIndex.load(std::memory_order_relaxed);
	const uint32 CurrentRead = ReadIndex.load(std::memory_order_acquire);
	if (CurrentWrite - CurrentRead >= Capacity)
	{
		return false;
	}

	FSlot& Slot = Slots[CurrentWrite % Capacity];
	const SIZE_T NumBytes = static_cast<SIZE_T>(NumFrames) * NumChannels * sizeof(float);
	FMemory::Memcpy(Slot.Data.Get(), Data, NumBytes);
	Slot.NumFrames = NumFrames;
	Slot.NumChannels = NumChannels;
	Slot.SampleRate = SampleRate;
	WriteIndex.store(CurrentWrite + 1, std::memory_order_release);
	return true;
}

bool FWwiseAudioQueue::TryPeek(FWwiseAudioFrame& OutFrame) const
{
	const uint32 CurrentRead = ReadIndex.load(std::memory_order_relaxed);
	if (CurrentRead == WriteIndex.load(std::memory_order_acquire))
	{
		return false;
	}

	const FSlot& Slot = Slots[CurrentRead % Capacity];
	OutFrame.Data = Slot.Data.Get();
	OutFrame.NumFrames = Slot.NumFrames;
	OutFrame.NumChannels = Slot.NumChannels;
	OutFrame.SampleRate = Slot.SampleRate;
	return true;
}

void FWwiseAudioQueue::Pop()
{
	const uint32 CurrentRead = ReadIndex.load(std::memory_order_relaxed);
	if (CurrentRead != WriteIndex.load(std::memory_order_acquire))
	{
		ReadIndex.store(CurrentRead + 1, std::memory_order_release);
	}
}
