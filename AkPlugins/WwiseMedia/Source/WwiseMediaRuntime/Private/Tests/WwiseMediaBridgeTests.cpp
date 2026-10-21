#if WITH_DEV_AUTOMATION_TESTS

#include "WwiseMediaAudioBridge.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
class FTestAudioSample final : public IMediaAudioSample
{
public:
    FTestAudioSample(float Left, float Right, uint32 InRate, uint32 InFrames, double Seconds = 0)
        : Rate(InRate), Frames(InFrames), Time(FTimespan::FromSeconds(Seconds))
    {
        Data.SetNumUninitialized(Frames * 2);
        for (uint32 Index = 0; Index < Frames; ++Index)
        {
            Data[Index * 2] = Left;
            Data[Index * 2 + 1] = Right;
        }
    }
    virtual const void* GetBuffer() override { return Data.GetData(); }
    virtual uint32 GetChannels() const override { return 2; }
    virtual FTimespan GetDuration() const override { return FTimespan::FromSeconds(static_cast<double>(Frames) / Rate); }
    virtual EMediaAudioSampleFormat GetFormat() const override { return EMediaAudioSampleFormat::Float; }
    virtual uint32 GetFrames() const override { return Frames; }
    virtual uint32 GetSampleRate() const override { return Rate; }
    virtual FMediaTimeStamp GetTime() const override { return Time; }
private:
    TArray<float> Data;
    uint32 Rate;
    uint32 Frames;
    FMediaTimeStamp Time;
};

bool WaitForPriming(const TSharedRef<FWwiseMediaAudioBridge, ESPMode::ThreadSafe>& Bridge)
{
    const double Deadline = FPlatformTime::Seconds() + 3;
    while (!Bridge->HasPrimed() && FPlatformTime::Seconds() < Deadline)
        FPlatformProcess::Sleep(0.001f);
    return Bridge->HasPrimed();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWwiseMediaResampleTest, "WwiseMedia.Bridge.ResampleAndPause",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWwiseMediaResampleTest::RunTest(const FString& Parameters)
{
    auto Bridge = MakeShared<FWwiseMediaAudioBridge, ESPMode::ThreadSafe>(8, 8, 10, 0);
    TestTrue(TEXT("Worker starts"), Bridge->StartWorker());
    Bridge->GetSampleQueue()->Enqueue(MakeShared<FTestAudioSample, ESPMode::ThreadSafe>(0.2f, 0.6f, 44100, 4410));
    Bridge->SetPlayback(true, FTimespan::Zero());
    if (!TestTrue(TEXT("44.1 kHz stereo input primes 48 kHz mono output"), WaitForPriming(Bridge)))
    { Bridge->Shutdown(); return false; }
    float Samples[512];
    float* Output[] = {Samples};
    Bridge->Fill(1, 512, Output);
    for (float Value : Samples)
        if (!FMath::IsNearlyEqual(Value, 0.4f, 0.001f)) { AddError(TEXT("Stereo downmix or resampling corrupted samples")); break; }
    const auto Consumed = Bridge->GetConsumedFrames();
    Bridge->SetPlayback(false, FTimespan::Zero());
    Bridge->Fill(1, 512, Output);
    TestEqual(TEXT("Paused audio does not consume PCM"), Bridge->GetConsumedFrames(), Consumed);
    for (float Value : Samples)
        if (Value != 0) { AddError(TEXT("Paused output must be initialized silence")); break; }
    FTimespan Time;
    TestTrue(TEXT("Consumed audio publishes a media clock"), Bridge->PumpMediaClock(Time));
    TestTrue(TEXT("Media sink receives valid clock"), Bridge->GetSampleQueue()->GetAudioTime().IsValid());
    Bridge->SetPlayback(true, FTimespan::Zero());
    Bridge->SetDraining(true);
    const double DrainDeadline = FPlatformTime::Seconds() + 3;
    do
    {
        Bridge->Fill(1, 512, Output);
        FPlatformProcess::Sleep(0.001f);
    }
    while (!Bridge->IsDrained() && FPlatformTime::Seconds() < DrainDeadline);
    const uint64 TotalFrames = Bridge->GetConsumedFrames();
    TestTrue(TEXT("100 ms at 44.1 kHz becomes approximately 4800 frames at 48 kHz"), TotalFrames >= 4798 && TotalFrames <= 4801);
    TestTrue(TEXT("Entire source drains without unbounded buffering"), Bridge->IsDrained());
    Bridge->Shutdown();
    Bridge->Fill(1, 512, Output);
    for (float Value : Samples)
        if (Value != 0) { AddError(TEXT("Stopped callbacks must be safe and silent")); break; }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWwiseMediaFlushTest, "WwiseMedia.Bridge.FlushRejectsOldAudio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWwiseMediaFlushTest::RunTest(const FString& Parameters)
{
    auto Bridge = MakeShared<FWwiseMediaAudioBridge, ESPMode::ThreadSafe>(8, 8, 10, 0);
    Bridge->StartWorker();
    Bridge->GetSampleQueue()->Enqueue(MakeShared<FTestAudioSample, ESPMode::ThreadSafe>(0.6f, 0.6f, 48000, 4800));
    Bridge->SetPlayback(true, FTimespan::Zero());
    if (!TestTrue(TEXT("Initial data primes"), WaitForPriming(Bridge))) { Bridge->Shutdown(); return false; }
    Bridge->Discontinuity(false);
    Bridge->GetSampleQueue()->RequestFlush();
    FTimespan OldTime;
    TestFalse(TEXT("A flushed generation cannot publish a stale clock"), Bridge->PumpMediaClock(OldTime));
    Bridge->GetSampleQueue()->Enqueue(MakeShared<FTestAudioSample, ESPMode::ThreadSafe>(-0.4f, -0.4f, 48000, 4800, 10));
    Bridge->SetProductionEnabled(true);
    float Samples[512];
    float* Output[] = {Samples};
    bool bNewAudio = false;
    const double Deadline = FPlatformTime::Seconds() + 3;
    while (!bNewAudio && FPlatformTime::Seconds() < Deadline)
    {
        Bridge->Fill(1, 512, Output);
        for (float Value : Samples)
        {
            if (Value > 0.001f) { AddError(TEXT("Old audio crossed the seek/flush boundary")); Bridge->Shutdown(); return false; }
            bNewAudio |= Value < -0.1f;
        }
        FPlatformProcess::Sleep(0.001f);
    }
    TestTrue(TEXT("New epoch reaches output"), bNewAudio);
    TestTrue(TEXT("Consumed PTS belongs to seek target"), Bridge->GetLastConsumedSeconds() >= 10);
    Bridge->Shutdown();
    return true;
}

#endif
