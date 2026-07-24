#include "AkAudioSampler.h"

#include <atomic>

#include "AkAudioDevice.h"
#include "AkAudioSamplerModule.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

namespace
{
constexpr uint64 VisualizationQueueCapacity = 128;

struct FVisualizationQueueSlot
{
	std::atomic<uint64> Sequence;
	AkAudioBusHackerVisualizationData Data;
};

class FVisualizationQueue
{
public:
	FVisualizationQueue()
		: EnqueuePosition(0)
		, DequeuePosition(0)
	{
		for (uint64 Index = 0; Index < VisualizationQueueCapacity; ++Index)
		{
			Slots[Index].Sequence.store(Index, std::memory_order_relaxed);
		}
	}

	bool Enqueue(const AkAudioBusHackerVisualizationData& Data)
	{
		uint64 Position = EnqueuePosition.load(std::memory_order_relaxed);
		FVisualizationQueueSlot* Slot = nullptr;

		for (;;)
		{
			Slot = &Slots[Position % VisualizationQueueCapacity];
			const uint64 Sequence = Slot->Sequence.load(std::memory_order_acquire);
			const int64 Difference = static_cast<int64>(Sequence) - static_cast<int64>(Position);
			if (Difference == 0)
			{
				if (EnqueuePosition.compare_exchange_weak(
					Position,
					Position + 1,
					std::memory_order_relaxed))
				{
					break;
				}
			}
			else if (Difference < 0)
			{
				return false;
			}
			else
			{
				Position = EnqueuePosition.load(std::memory_order_relaxed);
			}
		}

		Slot->Data = Data;
		Slot->Sequence.store(Position + 1, std::memory_order_release);
		return true;
	}

	bool Dequeue(AkAudioBusHackerVisualizationData& OutData)
	{
		uint64 Position = DequeuePosition.load(std::memory_order_relaxed);
		FVisualizationQueueSlot* Slot = nullptr;

		for (;;)
		{
			Slot = &Slots[Position % VisualizationQueueCapacity];
			const uint64 Sequence = Slot->Sequence.load(std::memory_order_acquire);
			const int64 Difference = static_cast<int64>(Sequence) - static_cast<int64>(Position + 1);
			if (Difference == 0)
			{
				if (DequeuePosition.compare_exchange_weak(
					Position,
					Position + 1,
					std::memory_order_relaxed))
				{
					break;
				}
			}
			else if (Difference < 0)
			{
				return false;
			}
			else
			{
				Position = DequeuePosition.load(std::memory_order_relaxed);
			}
		}

		OutData = Slot->Data;
		Slot->Sequence.store(Position + VisualizationQueueCapacity, std::memory_order_release);
		return true;
	}

private:
	FVisualizationQueueSlot Slots[VisualizationQueueCapacity];
	std::atomic<uint64> EnqueuePosition;
	std::atomic<uint64> DequeuePosition;
};

FVisualizationQueue VisualizationQueue;
FCriticalSection VisualizationCacheMutex;
TMap<AkUInt32, AkAudioBusHackerVisualizationData> LatestVisualizationByBus;
AkAudioBusHackerVisualizationData LatestVisualization{};
bool HasLatestVisualization = false;

bool IsValidVisualizationData(const AkAudioBusHackerVisualizationData* Data)
{
	return Data
		&& Data->uVersion == AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION
		&& Data->uStructSize >= sizeof(AkAudioBusHackerVisualizationData);
}

void DrainVisualizationQueue()
{
	AkAudioBusHackerVisualizationData Data{};
	while (VisualizationQueue.Dequeue(Data))
	{
		if (!IsValidVisualizationData(&Data))
		{
			continue;
		}

		LatestVisualizationByBus.FindOrAdd(Data.uBusID) = Data;
		LatestVisualization = Data;
		HasLatestVisualization = true;
	}
}

void CopyVisualizationData(
	const AkAudioBusHackerVisualizationData& Source,
	const FString& BusName,
	FAkAudioBusHackerVisualizationData& Destination)
{
	Destination = FAkAudioBusHackerVisualizationData{};
	Destination.Sequence = static_cast<int64>(Source.uSequence);
	Destination.BusName = BusName;
	Destination.SampleRate = static_cast<int32>(Source.uSampleRate);
	Destination.ChannelConfig = static_cast<int64>(Source.uChannelConfig);
	Destination.NumChannels = static_cast<int32>(Source.uNumChannels);
	Destination.AnalyzedChannels = static_cast<int32>(Source.uAnalyzedChannels);
	Destination.Frames = static_cast<int32>(Source.uFrames);
	Destination.SpectrumMinHz = Source.fSpectrumMinHz;
	Destination.SpectrumMaxHz = Source.fSpectrumMaxHz;
	Destination.StereoCorrelation = Source.fStereoCorrelation;
	Destination.DownstreamGain = Source.fDownstreamGain;

	const int32 ChannelCount = FMath::Min(
		static_cast<int32>(Source.uAnalyzedChannels),
		static_cast<int32>(AK_AUDIO_BUS_HACKER_MAX_CHANNELS));
	Destination.ChannelPeakDb.Append(Source.fChannelPeakDb, ChannelCount);
	Destination.ChannelRmsDb.Append(Source.fChannelRmsDb, ChannelCount);

	const int32 WaveformBinCount = FMath::Min(
		static_cast<int32>(Source.uWaveformBins),
		static_cast<int32>(AK_AUDIO_BUS_HACKER_WAVEFORM_BINS));
	Destination.WaveformMin.Append(Source.fWaveformMin, WaveformBinCount);
	Destination.WaveformMax.Append(Source.fWaveformMax, WaveformBinCount);

	const int32 SpectrumBinCount = FMath::Min(
		static_cast<int32>(Source.uSpectrumBins),
		static_cast<int32>(AK_AUDIO_BUS_HACKER_SPECTRUM_BINS));
	Destination.SpectrumDb.Append(Source.fSpectrumDb, SpectrumBinCount);
	Destination.SpectrumFrequenciesHz.Reserve(SpectrumBinCount);

	const float FrequencyRatio = Source.fSpectrumMinHz > 0.0f
		? Source.fSpectrumMaxHz / Source.fSpectrumMinHz
		: 0.0f;
	for (int32 Bin = 0; Bin < SpectrumBinCount; ++Bin)
	{
		const float Alpha = SpectrumBinCount > 1
			? static_cast<float>(Bin) / static_cast<float>(SpectrumBinCount - 1)
			: 0.0f;
		const float Frequency = FrequencyRatio > 0.0f
			? Source.fSpectrumMinHz * FMath::Pow(FrequencyRatio, Alpha)
			: 0.0f;
		Destination.SpectrumFrequenciesHz.Add(Frequency);
	}
}
} // namespace

UAkAudioSampler::UAkAudioSampler(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UAkAudioSampler::RegisterCatchBuffer()
{
	return FAkAudioSamplerModule::SetVisualizationCallback(&UAkAudioSampler::VisualizationCallback);
}

int32 UAkAudioSampler::UnregisterCatchBuffer()
{
	return FAkAudioSamplerModule::SetVisualizationCallback(nullptr);
}

bool UAkAudioSampler::IsVisualizationAvailable()
{
	return FAkAudioSamplerModule::IsVisualizationCallbackAvailable();
}

bool UAkAudioSampler::GetLatestVisualizationData(
	const FString& BusName,
	FAkAudioBusHackerVisualizationData& OutData)
{
	const AkUInt32 BusID = FAkAudioDevice::GetShortIDFromString(BusName);
	if (BusID == AK_INVALID_UNIQUE_ID)
	{
		OutData = FAkAudioBusHackerVisualizationData{};
		return false;
	}

	FScopeLock Lock(&VisualizationCacheMutex);
	DrainVisualizationQueue();

	const AkAudioBusHackerVisualizationData* Source = LatestVisualizationByBus.Find(BusID);
	if (!Source)
	{
		OutData = FAkAudioBusHackerVisualizationData{};
		return false;
	}

	CopyVisualizationData(*Source, BusName, OutData);
	return true;
}

TArray<float> UAkAudioSampler::UpdateSampleSpecturmCallback(float DeltaTime, int32& Tick)
{
	(void)DeltaTime;
	FScopeLock Lock(&VisualizationCacheMutex);
	DrainVisualizationQueue();

	if (HasLatestVisualization)
	{
		Tick = static_cast<int32>(LatestVisualization.uSequence & MAX_int32);
		const int32 SpectrumBinCount = FMath::Min(
			static_cast<int32>(LatestVisualization.uSpectrumBins),
			static_cast<int32>(AK_AUDIO_BUS_HACKER_SPECTRUM_BINS));
		TArray<float> Spectrum;
		Spectrum.Append(LatestVisualization.fSpectrumDb, SpectrumBinCount);
		return Spectrum;
	}

	Tick = 0;
	return TArray<float>();
}

void UAkAudioSampler::StopEventByID(int32 ID)
{
	if (IWwiseSoundEngineAPI* SoundEngine = IWwiseSoundEngineAPI::Get())
	{
		SoundEngine->StopPlayingID(static_cast<AkPlayingID>(ID));
	}
}

void UAkAudioSampler::VisualizationCallback(const AkAudioBusHackerVisualizationData* InData)
{
	if (!IsValidVisualizationData(InData))
	{
		return;
	}

	if (!VisualizationQueue.Enqueue(*InData))
	{
		AkAudioBusHackerVisualizationData DroppedData{};
		VisualizationQueue.Dequeue(DroppedData);
		VisualizationQueue.Enqueue(*InData);
	}
}
