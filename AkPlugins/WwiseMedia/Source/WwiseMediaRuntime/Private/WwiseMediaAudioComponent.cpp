#include "WwiseMediaAudioComponent.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "WwiseMediaAudioBridge.h"
#include "HAL/PlatformTime.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWwiseMedia, Log, All);

namespace
{
// Game-thread-only ownership; entries never keep players or components alive.
TMap<TWeakObjectPtr<UMediaPlayer>, TWeakObjectPtr<UWwiseMediaAudioComponent>> ActiveConsumers;
}

UWwiseMediaAudioComponent::UWwiseMediaAudioComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UWwiseMediaAudioComponent::BeginPlay()
{
    Super::BeginPlay();
    BindPlayer();
    if (bAutoPlay && MediaPlayer && MediaPlayer->IsReady()) PlayMedia();
}

void UWwiseMediaAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopAudioInput();
    UnbindPlayer();
    Super::EndPlay(Reason);
}

void UWwiseMediaAudioComponent::OnUnregister()
{
    StopAudioInput();
    UnbindPlayer();
    Super::OnUnregister();
}

void UWwiseMediaAudioComponent::PostUnregisterGameObject()
{
    StopAudioInput();
    Super::PostUnregisterGameObject();
}

void UWwiseMediaAudioComponent::BindPlayer()
{
    if (BoundPlayer.Get() == MediaPlayer.Get()) return;
    UnbindPlayer();
    if (!MediaPlayer) return;
    MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &ThisClass::HandleOpened);
    MediaPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &ThisClass::HandleOpenFailed);
    MediaPlayer->OnMediaClosed.AddUniqueDynamic(this, &ThisClass::HandleClosed);
    MediaPlayer->OnEndReached.AddUniqueDynamic(this, &ThisClass::HandleEndReached);
    MediaPlayer->OnSeekCompleted.AddUniqueDynamic(this, &ThisClass::HandleSeekCompleted);
    BoundPlayer = MediaPlayer;
}

void UWwiseMediaAudioComponent::UnbindPlayer()
{
    if (UMediaPlayer* Player = BoundPlayer.Get())
    {
        Player->OnMediaOpened.RemoveDynamic(this, &ThisClass::HandleOpened);
        Player->OnMediaOpenFailed.RemoveDynamic(this, &ThisClass::HandleOpenFailed);
        Player->OnMediaClosed.RemoveDynamic(this, &ThisClass::HandleClosed);
        Player->OnEndReached.RemoveDynamic(this, &ThisClass::HandleEndReached);
        Player->OnSeekCompleted.RemoveDynamic(this, &ThisClass::HandleSeekCompleted);
    }
    BoundPlayer.Reset();
}

void UWwiseMediaAudioComponent::SetMediaPlayer(UMediaPlayer* Player)
{
    check(IsInGameThread());
    if (Player != MediaPlayer)
    {
        StopAudioInput();
        UnbindPlayer();
        MediaPlayer = Player;
        ChangeState(EWwiseMediaPlaybackState::Closed);
    }
    BindPlayer();
}

bool UWwiseMediaAudioComponent::OpenMediaSource(UMediaSource* Source, bool bInPlayWhenOpened)
{
    check(IsInGameThread());
    if (!MediaPlayer || !Source) { Fail(TEXT("Assign a Media Player and a valid Media Source.")); return false; }
    if (const auto* Existing = ActiveConsumers.Find(MediaPlayer))
    {
        if (Existing->IsValid() && Existing->Get() != this)
        {
            Fail(TEXT("This Media Player belongs to another active WwiseMedia component."));
            return false;
        }
    }
    StopAudioInput();
    BindPlayer();
    LastError.Reset();
    MediaPlayer->NativeAudioOut = false;
    bPlayWhenOpened = bInPlayWhenOpened;
    ChangeState(EWwiseMediaPlaybackState::Opening);
    if (!MediaPlayer->OpenSource(Source))
    {
        Fail(TEXT("The media backend rejected OpenSource."));
        return false;
    }
    return true;
}

bool UWwiseMediaAudioComponent::PrepareBridge()
{
    if (Bridge) return true;
    if (!IsRegistered() || !MediaPlayer || !AkAudioEvent)
    {
        Fail(TEXT("Register the WwiseMedia component and assign Media Player and Ak Audio Event."));
        return false;
    }
    if (MediaPlayer->NativeAudioOut)
    {
        Fail(TEXT("Disable Native Audio Out and reopen the media before starting WwiseMedia."));
        return false;
    }
    if (MediaPlayer->GetNumTracks(EMediaPlayerTrack::Audio) == 0)
    {
        Fail(TEXT("The opened media has no available audio track."));
        return false;
    }
    if (MediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Audio) == INDEX_NONE
        && !MediaPlayer->SelectTrack(EMediaPlayerTrack::Audio, 0))
    {
        Fail(TEXT("The media backend could not select an audio track."));
        return false;
    }
    if (const auto* Existing = ActiveConsumers.Find(MediaPlayer))
    {
        if (Existing->IsValid() && Existing->Get() != this)
        {
            Fail(TEXT("This Media Player already has an active WwiseMedia consumer. Use independent Media Players for independent screens."));
            return false;
        }
    }
    for (TObjectIterator<UMediaSoundComponent> It; It; ++It)
    {
        if (!It->IsTemplate() && It->IsRegistered() && It->IsActive() && It->GetMediaPlayer() == MediaPlayer)
        {
            Fail(TEXT("An active Media Sound Component is consuming this Media Player. Stop or unbind it before using WwiseMedia."));
            return false;
        }
    }
    Bridge = MakeShared<FWwiseMediaAudioBridge, ESPMode::ThreadSafe>(
        MaximumMediaPackets, PcmBufferBlocks, PrebufferMilliseconds, OutputLatencyMilliseconds);
    if (!Bridge->StartWorker())
    {
        Fail(TEXT("Could not start the media audio conversion worker."));
        return false;
    }
    ActiveConsumers.Add(MediaPlayer, this);
    MediaPlayer->GetPlayerFacade()->AddAudioSampleSink(Bridge->GetSampleQueue());
    if (!AkAudioEvent->IsLoaded()) AkAudioEvent->LoadData();
    return true;
}

bool UWwiseMediaAudioComponent::PlayMedia()
{
    check(IsInGameThread());
    BindPlayer();
    if (!MediaPlayer || !MediaPlayer->IsReady())
    {
        Fail(TEXT("Open a Media Source and wait for On Media Opened before calling Play Media."));
        return false;
    }
    if (bWaitingForSeek) { bResumeAfterSeek = true; return true; }
    if (bReachedEnd) StopAudioInput();
    if (!PrepareBridge()) return false;
    LastError.Reset();
    if (!MediaPlayer->Play()) { Fail(TEXT("The media backend rejected playback.")); return false; }
    Bridge->SetPlayback(true, MediaPlayer->GetTime());
    StartRequestedAt = FPlatformTime::Seconds();
    ChangeState(ActivePlayingID == AK_INVALID_PLAYING_ID ? EWwiseMediaPlaybackState::Priming : EWwiseMediaPlaybackState::Playing);
    return true;
}

bool UWwiseMediaAudioComponent::PauseMedia()
{
    check(IsInGameThread());
    if (!MediaPlayer || !Bridge) return false;
    bResumeAfterSeek = false;
    if (!MediaPlayer->Pause()) return false;
    Bridge->SetPlayback(false, MediaPlayer->GetTime());
    ChangeState(EWwiseMediaPlaybackState::Paused);
    return true;
}

bool UWwiseMediaAudioComponent::SeekMedia(FTimespan Time)
{
    check(IsInGameThread());
    if (!MediaPlayer || !Bridge || Time < FTimespan::Zero() || !MediaPlayer->SupportsSeeking()) return false;
    if (!bWaitingForSeek) bResumeAfterSeek = MediaPlayer->IsPlaying();
    bWaitingForSeek = true;
    bReachedEnd = false;
    Bridge->SetDraining(false);
    Bridge->Discontinuity(false);
    MediaPlayer->GetPlayerFacade()->SetLastAudioRenderedSampleTime(FTimespan::MinValue());
    ChangeState(EWwiseMediaPlaybackState::Seeking);
    if (!MediaPlayer->Seek(Time))
    {
        bWaitingForSeek = false;
        Bridge->SetProductionEnabled(true);
        ChangeState(MediaPlayer->IsPlaying() ? EWwiseMediaPlaybackState::Rebuffering : EWwiseMediaPlaybackState::Paused);
        return false;
    }
    return true;
}

void UWwiseMediaAudioComponent::StopAudioInput()
{
    if (Bridge) Bridge->SetProductionEnabled(false);
    if (ActivePlayingID != AK_INVALID_PLAYING_ID)
    {
        if (auto* Device = FAkAudioDevice::Get()) Device->StopPlayingID(ActivePlayingID);
        ActivePlayingID = AK_INVALID_PLAYING_ID;
        // AudioInputManager removes the shared-state delegates on EndOfEvent.
        // Its Stop helper mutates the global map without taking its read lock.
    }
    if (Bridge)
    {
        Bridge->Shutdown();
        if (MediaPlayer)
            MediaPlayer->GetPlayerFacade()->SetLastAudioRenderedSampleTime(FTimespan::MinValue());
        Bridge.Reset();
    }
    if (const auto* Existing = ActiveConsumers.Find(MediaPlayer))
    {
        if (Existing->Get() == this) ActiveConsumers.Remove(MediaPlayer);
    }
    bWaitingForSeek = false;
    bReachedEnd = false;
    TailDrainedAt = 0;
    EventPostedAt = 0;
}

void UWwiseMediaAudioComponent::StopMedia(bool bClosePlayer)
{
    check(IsInGameThread());
    StopAudioInput();
    if (MediaPlayer)
    {
        if (bClosePlayer) MediaPlayer->Close();
        else MediaPlayer->Pause();
    }
    ChangeState(EWwiseMediaPlaybackState::Closed);
}

void UWwiseMediaAudioComponent::Stop()
{
    // Called by AkComponent destruction too: never pause a player's other owner.
    const bool bOwnedPlayback = Bridge.IsValid();
    StopAudioInput();
    if (bOwnedPlayback && MediaPlayer) MediaPlayer->Pause();
}

int32 UWwiseMediaAudioComponent::PostAssociatedAudioInputEvent()
{
    check(IsInGameThread());
    if (ActivePlayingID != AK_INVALID_PLAYING_ID) return static_cast<int32>(ActivePlayingID);
    if (!Bridge || !AkAudioEvent || !AkAudioEvent->IsLoaded() || !AkAudioEvent->IsDataFullyLoaded())
        return static_cast<int32>(AK_INVALID_PLAYING_ID);
    ActivePlayingID = FAkAudioInputManager::PostAudioInputEvent(AkAudioEvent, this,
        FAkGlobalAudioInputDelegate::CreateSP(Bridge.ToSharedRef(), &FWwiseMediaAudioBridge::Fill),
        FAkGlobalAudioFormatDelegate::CreateSP(Bridge.ToSharedRef(), &FWwiseMediaAudioBridge::GetFormat));
    EventPostedAt = FPlatformTime::Seconds();
    return static_cast<int32>(ActivePlayingID);
}

bool UWwiseMediaAudioComponent::FillSamplesBuffer(uint32 Channels, uint32 Frames, float** Output)
{
    // The manager binds shared bridge state directly, never the UObject's this.
    for (uint32 Channel = 0; Channel < Channels; ++Channel)
        FMemory::Memzero(Output[Channel], Frames * sizeof(float));
    return true;
}

void UWwiseMediaAudioComponent::GetChannelConfig(AkAudioFormat& Format)
{
    Format.uSampleRate = WwiseMedia::OutputSampleRate;
    Format.channelConfig.SetStandard(AK_SPEAKER_SETUP_MONO);
}

void UWwiseMediaAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!Bridge || !MediaPlayer) return;
    const double Now = FPlatformTime::Seconds();
    const float Rate = MediaPlayer->GetRate();
    if (!bReachedEnd && !FMath::IsNearlyZero(Rate) && !FMath::IsNearlyEqual(Rate, 1.0f))
    {
        Fail(TEXT("WwiseMedia currently supports forward 1.0x playback and pause only."));
        return;
    }
    Bridge->SetPlayback(!bWaitingForSeek && (MediaPlayer->IsPlaying() || bReachedEnd), MediaPlayer->GetTime());
    FTimespan RenderedTime;
    if (Bridge->PumpMediaClock(RenderedTime))
        MediaPlayer->GetPlayerFacade()->SetLastAudioRenderedSampleTime(RenderedTime);

    if (ActivePlayingID == AK_INVALID_PLAYING_ID && !bWaitingForSeek && Bridge->HasPrimed()
        && (MediaPlayer->IsPlaying() || bReachedEnd))
    {
        if (PostAssociatedAudioInputEvent() == static_cast<int32>(AK_INVALID_PLAYING_ID)
            && AkAudioEvent && AkAudioEvent->IsLoaded() && AkAudioEvent->IsDataFullyLoaded())
        {
            Fail(TEXT("Wwise rejected the Audio Input event. Check SoundEngine, event target and game object registration."));
            return;
        }
    }
    if (ActivePlayingID == AK_INVALID_PLAYING_ID && Now - StartRequestedAt > 15 && !bWaitingForSeek
        && !MediaPlayer->IsPaused())
    {
        Fail(TEXT("No playable PCM or loaded Wwise event after 15 seconds. Check primary sink, audio format and SoundBank loading."));
        return;
    }
    if (ActivePlayingID != AK_INVALID_PLAYING_ID && !bWaitingForSeek && !MediaPlayer->IsPaused())
    {
        const double LastCallback = FMath::Max(EventPostedAt, Bridge->GetLastCallbackTime());
        if (Now - LastCallback > FMath::Max(0.5f, CallbackTimeoutSeconds))
        {
            Fail(TEXT("Wwise stopped requesting Audio Input. Check virtual voices, voice limits and Continue to play settings."));
            return;
        }
    }
    if (bReachedEnd)
    {
        if (Bridge->IsDrained())
        {
            if (!TailDrainedAt) TailDrainedAt = Now;
            if (Now - TailDrainedAt > 0.05 + OutputLatencyMilliseconds * 0.001)
            {
                StopAudioInput();
                ChangeState(EWwiseMediaPlaybackState::Ended);
            }
        }
        else TailDrainedAt = 0;
        return;
    }
    if (bWaitingForSeek) return;
    if (MediaPlayer->IsPaused()) ChangeState(EWwiseMediaPlaybackState::Paused);
    else if (MediaPlayer->IsPlaying())
    {
        if (ActivePlayingID == AK_INVALID_PLAYING_ID) ChangeState(EWwiseMediaPlaybackState::Priming);
        else ChangeState(Bridge->BufferedMs() > 0 ? EWwiseMediaPlaybackState::Playing : EWwiseMediaPlaybackState::Rebuffering);
    }
}

void UWwiseMediaAudioComponent::HandleOpened(FString Url)
{
    StopAudioInput();
    if (bPlayWhenOpened || bAutoPlay) PlayMedia();
    else ChangeState(EWwiseMediaPlaybackState::Paused);
}

void UWwiseMediaAudioComponent::HandleOpenFailed(FString Url)
{
    Fail(FString::Printf(TEXT("Media open failed: %s"), *Url));
}

void UWwiseMediaAudioComponent::HandleClosed()
{
    StopAudioInput();
    ChangeState(EWwiseMediaPlaybackState::Closed);
}

void UWwiseMediaAudioComponent::HandleEndReached()
{
    if (!Bridge || (MediaPlayer && MediaPlayer->IsLooping())) return;
    bReachedEnd = true;
    EndReachedAt = FPlatformTime::Seconds();
    Bridge->SetDraining(true);
    Bridge->SetPlayback(true, MediaPlayer ? MediaPlayer->GetTime() : FTimespan::Zero());
}

void UWwiseMediaAudioComponent::HandleSeekCompleted()
{
    if (!Bridge) return;
    // Also discard stale PCM when a caller used MediaPlayer.Seek directly.
    Bridge->Discontinuity(true);
    bWaitingForSeek = false;
    StartRequestedAt = FPlatformTime::Seconds();
    if (bResumeAfterSeek) MediaPlayer->Play();
    Bridge->SetPlayback(MediaPlayer->IsPlaying(), MediaPlayer->GetTime());
    ChangeState(MediaPlayer->IsPlaying() ? EWwiseMediaPlaybackState::Rebuffering : EWwiseMediaPlaybackState::Paused);
}

void UWwiseMediaAudioComponent::ChangeState(EWwiseMediaPlaybackState NewState)
{
    if (PlaybackState == NewState) return;
    PlaybackState = NewState;
    OnStateChanged.Broadcast(NewState);
}

void UWwiseMediaAudioComponent::Fail(const FString& Message)
{
    LastError = Message;
    const bool bOwnedPlayback = Bridge.IsValid();
    StopAudioInput();
    if (bOwnedPlayback && MediaPlayer) MediaPlayer->Pause();
    UE_LOG(LogWwiseMedia, Error, TEXT("%s: %s"), *GetName(), *Message);
    ChangeState(EWwiseMediaPlaybackState::Error);
    OnPlaybackError.Broadcast(Message);
}

FWwiseMediaDebugInfo UWwiseMediaAudioComponent::GetDebugInfo() const
{
    FWwiseMediaDebugInfo Info;
    Info.State = PlaybackState;
    Info.PlayingID = ActivePlayingID;
    Info.LastError = LastError;
    if (Bridge)
    {
        Info.PlaybackEpoch = Bridge->GetEpoch();
        Info.BufferedMilliseconds = Bridge->BufferedMs();
        Info.UnderrunFrames = Bridge->GetUnderrunFrames();
        Info.RejectedSamples = Bridge->GetSampleQueue()->Rejected.load();
        Info.InputSampleRate = Bridge->GetSampleQueue()->InputRate.load();
        Info.InputChannels = Bridge->GetSampleQueue()->InputChannels.load();
        Info.LastConsumedMediaSeconds = Bridge->GetLastConsumedSeconds();
    }
    return Info;
}
