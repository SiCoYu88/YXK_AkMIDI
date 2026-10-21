#pragma once

#include "CoreMinimal.h"
#include "AkAudioInputManager.h"
#include "WwiseMediaPcmQueue.h"
#include "HAL/Runnable.h"
#include "MediaAudioResampler.h"
#include "MediaSampleQueue.h"
#include <atomic>

class FEvent;
class FRunnableThread;

/** Media producers may flush this queue independently of both audio threads. */
class FWwiseMediaSampleQueue final : public FMediaAudioSampleQueue
{
public:
    explicit FWwiseMediaSampleQueue(int32 MaximumPackets) : FMediaAudioSampleQueue(MaximumPackets) {}
    virtual bool Enqueue(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample) override;
    virtual void RequestFlush() override;
    virtual bool CanAcceptSamples(int32 Count) const override;
    uint32 GetRevision() const { return Revision.load(std::memory_order_acquire); }
    bool PublishAudioTime(const FMediaTimeStampSample& Time, uint32 ExpectedRevision);

    std::atomic<uint64> Rejected{0};
    std::atomic<uint32> InputRate{0};
    std::atomic<uint32> InputChannels{0};

private:
    // Guards clock publication against concurrent Flush. Not acquired by Wwise.
    FCriticalSection ClockLock;
    std::atomic<uint32> Revision{0};
};

/** Shared callbacks never touch a UObject. The worker owns its resampler. */
class FWwiseMediaAudioBridge final
    : public TSharedFromThis<FWwiseMediaAudioBridge, ESPMode::ThreadSafe>, public FRunnable
{
public:
    FWwiseMediaAudioBridge(int32 MaximumPackets, int32 BufferBlocks, float PrebufferMs, float LatencyMs);
    ~FWwiseMediaAudioBridge();
    bool StartWorker();
    void Shutdown();
    void SetPlayback(bool bPlaying, FTimespan MediaTime);
    void Discontinuity(bool bAllowProduction);
    void SetProductionEnabled(bool bEnabled);
    void SetDraining(bool bEnabled);
    bool Fill(uint32 Channels, uint32 Frames, float** Output);
    void GetFormat(AkAudioFormat& Format);
    bool PumpMediaClock(FTimespan& OutTime);
    bool HasPrimed() const;
    double BufferedMs() const;
    uint64 GetEpoch() const { return Epoch.load(); }
    uint64 GetUnderrunFrames() const { return UnderrunFrames.load(); }
    double GetLastCallbackTime() const { return LastCallbackTime.load(); }
    double GetLastConsumedSeconds() const { return ConsumedTicks.load() / 10000000.0; }
    uint64 GetConsumedFrames() const { return ConsumedFrames.load(); }
    uint64 GetProducedFrames() const { return ProducedFrames.load(); }
    bool IsDrained() const { return PcmQueue.Num() == 0 && bProducerIdle.load(); }
    TSharedRef<FWwiseMediaSampleQueue, ESPMode::ThreadSafe> GetSampleQueue() const { return SampleQueue; }
    virtual uint32 Run() override;

private:
    void PushSegment(const float* Samples, uint32 Frames, FMediaTimeStamp FirstTime, uint64 EpochValue, uint32 Revision);

    TSharedRef<FWwiseMediaSampleQueue, ESPMode::ThreadSafe> SampleQueue;
    WwiseMedia::FPcmQueue PcmQueue;
    FMediaAudioResampler Resampler;
    TUniquePtr<FRunnableThread> Worker;
    FEvent* WakeEvent = nullptr;
    const uint32 PrimeBlocks;
    const double LatencySeconds;
    std::atomic<bool> bShutdown{false};
    std::atomic<bool> bPlayback{false};
    std::atomic<bool> bProduction{true};
    std::atomic<bool> bDraining{false};
    std::atomic<bool> bProducerIdle{false};
    std::atomic<uint64> Epoch{1};
    std::atomic<int64> MediaTimeTicks{0};
    std::atomic<uint64> UnderrunFrames{0};
    std::atomic<uint64> ProducedFrames{0};
    std::atomic<uint64> ConsumedFrames{0};
    std::atomic<double> LastCallbackTime{0};
    std::atomic<double> LastProductionTime{0};
    std::atomic<int64> ConsumedTicks{-10000000};
    // A try-lock on the audio side prevents blocking on clock readers.
    FCriticalSection FeedbackLock;
    FMediaTimeStampSample Feedback;
    uint64 FeedbackEpoch = 0;
    uint32 FeedbackRevision = 0;
    uint64 ConsumerEpoch = 0;
    uint32 ConsumerRevision = 0;
    uint32 ReadOffset = 0;
    bool bConsumerPrimed = false;
};
