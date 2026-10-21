#include "WwiseMediaAudioBridge.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

namespace
{
FTimespan FrameDuration(uint32 Frames)
{
    return FTimespan::FromSeconds(static_cast<double>(Frames) / WwiseMedia::OutputSampleRate);
}
}

bool FWwiseMediaSampleQueue::Enqueue(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample)
{
    InputRate.store(Sample->GetSampleRate());
    InputChannels.store(Sample->GetChannels());
    const auto Format = Sample->GetFormat();
    const bool bKnownFormat = Format == EMediaAudioSampleFormat::Float
        || Format == EMediaAudioSampleFormat::Double || Format == EMediaAudioSampleFormat::Int8
        || Format == EMediaAudioSampleFormat::Int16 || Format == EMediaAudioSampleFormat::Int32;
    if (!bKnownFormat || !Sample->GetBuffer() || Sample->GetChannels() < 1
        || Sample->GetChannels() > 8 || Sample->GetSampleRate() < 8000
        || Sample->GetSampleRate() > 384000 || Sample->GetFrames() == 0
        || Sample->GetFrames() > Sample->GetSampleRate() * 2
        || Sample->GetDuration() <= FTimespan::Zero() || !Sample->GetTime().IsValid())
    {
        Rejected.fetch_add(1);
        return false;
    }
    return FMediaAudioSampleQueue::Enqueue(Sample);
}

bool FWwiseMediaSampleQueue::CanAcceptSamples(int32 Count) const
{
    FScopeLock Lock(&CriticalSection);
    return FMediaAudioSampleQueue::CanAcceptSamples(Count);
}

void FWwiseMediaSampleQueue::RequestFlush()
{
    FScopeLock Lock(&ClockLock);
    Revision.fetch_add(1, std::memory_order_acq_rel); // Odd while flushing.
    FMediaAudioSampleQueue::RequestFlush();
    Revision.fetch_add(1, std::memory_order_release);
}

bool FWwiseMediaSampleQueue::PublishAudioTime(const FMediaTimeStampSample& Time, uint32 ExpectedRevision)
{
    FScopeLock Lock(&ClockLock);
    if (Revision.load(std::memory_order_acquire) != ExpectedRevision) return false;
    SetAudioTime(Time);
    return true;
}

FWwiseMediaAudioBridge::FWwiseMediaAudioBridge(int32 MaximumPackets, int32 BufferBlocks, float PrebufferMs, float LatencyMs)
    : SampleQueue(MakeShared<FWwiseMediaSampleQueue, ESPMode::ThreadSafe>(FMath::Clamp(MaximumPackets, 2, 64)))
    , PcmQueue(FMath::Clamp(BufferBlocks, 3, 32))
    , PrimeBlocks(FMath::Clamp(FMath::CeilToInt(PrebufferMs * 0.001 * WwiseMedia::OutputSampleRate / WwiseMedia::FramesPerBlock), 1, FMath::Clamp(BufferBlocks, 3, 32) - 1))
    , LatencySeconds(FMath::Clamp(LatencyMs, 0.0f, 500.0f) * 0.001)
{
    WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
    Resampler.Initialize(1, WwiseMedia::OutputSampleRate);
}

FWwiseMediaAudioBridge::~FWwiseMediaAudioBridge()
{
    Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
}

bool FWwiseMediaAudioBridge::StartWorker()
{
    Worker.Reset(FRunnableThread::Create(this, TEXT("WwiseMediaConvert"), 0, TPri_AboveNormal));
    return Worker.IsValid();
}

void FWwiseMediaAudioBridge::Shutdown()
{
    bShutdown.store(true);
    bPlayback.store(false);
    WakeEvent->Trigger();
    if (Worker)
    {
        Worker->WaitForCompletion();
        Worker.Reset();
    }
}

void FWwiseMediaAudioBridge::SetPlayback(bool bPlaying, FTimespan MediaTime)
{
    MediaTimeTicks.store(MediaTime.GetTicks());
    bPlayback.store(bPlaying);
    WakeEvent->Trigger();
}

void FWwiseMediaAudioBridge::Discontinuity(bool bAllowProduction)
{
    bProduction.store(false);
    Epoch.fetch_add(1, std::memory_order_acq_rel);
    ConsumedTicks.store(-10000000);
    {
        FScopeLock Lock(&FeedbackLock);
        Feedback.Invalidate();
    }
    SampleQueue->InvalidateAudioTime();
    bProduction.store(bAllowProduction);
    WakeEvent->Trigger();
}

void FWwiseMediaAudioBridge::SetProductionEnabled(bool bEnabled)
{
    bProduction.store(bEnabled);
    WakeEvent->Trigger();
}

void FWwiseMediaAudioBridge::SetDraining(bool bEnabled)
{
    bDraining.store(bEnabled);
    WakeEvent->Trigger();
}

void FWwiseMediaAudioBridge::GetFormat(AkAudioFormat& Format)
{
    Format.uSampleRate = WwiseMedia::OutputSampleRate;
    Format.channelConfig.SetStandard(AK_SPEAKER_SETUP_MONO);
}

bool FWwiseMediaAudioBridge::HasPrimed() const
{
    const uint32 Blocks = PcmQueue.Num();
    return Blocks >= PrimeBlocks || (Blocks > 0 && (bDraining.load()
        || (bProducerIdle.load() && FPlatformTime::Seconds() - LastProductionTime.load() > 0.05)));
}

double FWwiseMediaAudioBridge::BufferedMs() const
{
    return PcmQueue.Num() * WwiseMedia::FramesPerBlock * 1000.0 / WwiseMedia::OutputSampleRate;
}

void FWwiseMediaAudioBridge::PushSegment(const float* Samples, uint32 Frames, FMediaTimeStamp FirstTime, uint64 EpochValue, uint32 Revision)
{
    if (!Frames) return;
    WwiseMedia::FPcmBlock Block;
    FMemory::Memcpy(Block.Samples.data(), Samples, Frames * sizeof(float));
    for (uint32 Index = 0; Index < Frames; ++Index)
    {
        float& Value = Block.Samples[Index];
        Value = FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.0f, 1.0f) : 0.0f;
    }
    Block.Epoch = EpochValue;
    Block.FlushVersion = Revision;
    Block.Frames = Frames;
    Block.FirstTimeTicks = FirstTime.Time.GetTicks();
    Block.Sequence = FirstTime.GetSequenceIndex();
    Block.Loop = FirstTime.GetLoopIndex();
    if (PcmQueue.Push(Block))
    {
        ProducedFrames.fetch_add(Frames);
        LastProductionTime.store(FPlatformTime::Seconds());
    }
}

uint32 FWwiseMediaAudioBridge::Run()
{
    uint64 WorkerEpoch = 0;
    uint32 WorkerRevision = 0;
    FMediaTimeStamp NextTime;
    float Scratch[WwiseMedia::FramesPerBlock];
    while (!bShutdown.load())
    {
        const uint64 CurrentEpoch = Epoch.load(std::memory_order_acquire);
        const uint32 Revision = SampleQueue->GetRevision();
        if (Revision & 1) { WakeEvent->Wait(2); continue; }
        if (CurrentEpoch != WorkerEpoch || Revision != WorkerRevision)
        {
            Resampler.Flush();
            NextTime.Invalidate();
            WorkerEpoch = CurrentEpoch;
            WorkerRevision = Revision;
            bProducerIdle.store(false);
        }
        if (!bProduction.load() || (!bPlayback.load() && !bDraining.load()) || PcmQueue.Free() < 2)
        {
            WakeEvent->Wait(2);
            continue;
        }
        uint32 JumpFrame = MAX_uint32;
        FMediaTimeStamp LastTime;
        const uint32 Frames = Resampler.Generate(Scratch, LastTime, WwiseMedia::FramesPerBlock,
            1.0f, FTimespan(MediaTimeTicks.load()), *SampleQueue, JumpFrame);
        if (CurrentEpoch != Epoch.load() || Revision != SampleQueue->GetRevision() || !bProduction.load())
            continue;
        bProducerIdle.store(Frames == 0);
        if (!Frames) { WakeEvent->Wait(2); continue; }

        // UE 5.8.3's implementation updates OutTime for every generated frame,
        // despite the header describing the first frame. It returns the last PTS.
        if (JumpFrame > 0 && JumpFrame < Frames && NextTime.IsValid())
        {
            PushSegment(Scratch, JumpFrame, NextTime, CurrentEpoch, Revision);
            PushSegment(Scratch + JumpFrame, Frames - JumpFrame,
                LastTime - FrameDuration(Frames - JumpFrame - 1), CurrentEpoch, Revision);
        }
        else
        {
            PushSegment(Scratch, Frames, LastTime - FrameDuration(Frames - 1), CurrentEpoch, Revision);
        }
        NextTime = LastTime + FrameDuration(1);
    }
    return 0;
}

bool FWwiseMediaAudioBridge::Fill(uint32 Channels, uint32 Frames, float** Output)
{
    for (uint32 Channel = 0; Channel < Channels; ++Channel)
        FMemory::Memzero(Output[Channel], Frames * sizeof(float));
    LastCallbackTime.store(FPlatformTime::Seconds());
    if (bShutdown.load() || Channels != 1) return true;

    const uint64 CurrentEpoch = Epoch.load(std::memory_order_acquire);
    const uint32 Revision = SampleQueue->GetRevision();
    if (Revision & 1) return true;
    if (ConsumerEpoch != CurrentEpoch || ConsumerRevision != Revision)
    {
        ConsumerEpoch = CurrentEpoch;
        ConsumerRevision = Revision;
        ReadOffset = 0;
        bConsumerPrimed = false;
    }
    while (const auto* Block = PcmQueue.Peek())
    {
        if (Block->Epoch == CurrentEpoch && Block->FlushVersion == Revision) break;
        ReadOffset = 0;
        PcmQueue.Pop();
    }
    if (!bProduction.load() || (!bPlayback.load() && !bDraining.load())) return true;
    if (!bConsumerPrimed)
    {
        if (!HasPrimed()) return true;
        bConsumerPrimed = true;
    }
    uint32 Written = 0;
    FMediaTimeStamp FirstTime;
    FMediaTimeStamp LastTime;
    while (Written < Frames)
    {
        const auto* Block = PcmQueue.Peek();
        if (!Block) break;
        if (Block->Epoch != CurrentEpoch || Block->FlushVersion != Revision)
        {
            PcmQueue.Pop();
            ReadOffset = 0;
            continue;
        }
        const uint32 Count = FMath::Min(Frames - Written, Block->Frames - ReadOffset);
        const FMediaTimeStamp SegmentTime(FTimespan(Block->FirstTimeTicks), Block->Sequence, Block->Loop);
        if (Written == 0) FirstTime = SegmentTime + FrameDuration(ReadOffset);
        FMemory::Memcpy(Output[0] + Written, Block->Samples.data() + ReadOffset, Count * sizeof(float));
        ReadOffset += Count;
        Written += Count;
        LastTime = SegmentTime + FrameDuration(ReadOffset);
        if (ReadOffset == Block->Frames) { PcmQueue.Pop(); ReadOffset = 0; }
    }
    if (CurrentEpoch != Epoch.load() || Revision != SampleQueue->GetRevision())
    {
        FMemory::Memzero(Output[0], Frames * sizeof(float));
        return true;
    }
    if (Written < Frames)
    {
        UnderrunFrames.fetch_add(Frames - Written);
        bConsumerPrimed = false;
    }
    if (Written)
    {
        ConsumedFrames.fetch_add(Written);
        ConsumedTicks.store(LastTime.Time.GetTicks());
        if (FeedbackLock.TryLock())
        {
            Feedback = FMediaTimeStampSample(FirstTime, FPlatformTime::Seconds() + LatencySeconds);
            FeedbackEpoch = CurrentEpoch;
            FeedbackRevision = Revision;
            FeedbackLock.Unlock();
        }
    }
    WakeEvent->Trigger();
    return true;
}

bool FWwiseMediaAudioBridge::PumpMediaClock(FTimespan& OutTime)
{
    FScopeLock Lock(&FeedbackLock);
    if (!Feedback.IsValid() || FeedbackEpoch != Epoch.load()
        || FeedbackRevision != SampleQueue->GetRevision()) return false;
    if (!SampleQueue->PublishAudioTime(Feedback, FeedbackRevision)) return false;
    OutTime = Feedback.TimeStamp.Time;
    return true;
}
