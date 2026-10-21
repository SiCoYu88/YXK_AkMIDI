#pragma once

#include "CoreMinimal.h"
#include "AkAudioInputComponent.h"
#include "WwiseMediaAudioComponent.generated.h"

class UMediaPlayer;
class UMediaSource;
class FWwiseMediaAudioBridge;

UENUM(BlueprintType)
enum class EWwiseMediaPlaybackState : uint8
{
    Closed, Opening, Priming, Playing, Paused, Seeking, Rebuffering, Ended, Error
};

USTRUCT(BlueprintType)
struct WWISEMEDIARUNTIME_API FWwiseMediaDebugInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    EWwiseMediaPlaybackState State = EWwiseMediaPlaybackState::Closed;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int64 PlayingID = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int64 PlaybackEpoch = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    float BufferedMilliseconds = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int64 UnderrunFrames = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int64 RejectedSamples = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int32 InputSampleRate = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    int32 InputChannels = 0;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    double LastConsumedMediaSeconds = -1;

    UPROPERTY(BlueprintReadOnly, Category="WwiseMedia")
    FString LastError;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWwiseMediaStateChanged, EWwiseMediaPlaybackState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWwiseMediaError, const FString&, Message);

/** One independent Media Player audio consumer and Wwise emitter per component. */
UCLASS(ClassGroup=(Audiokinetic), BlueprintType, meta=(BlueprintSpawnableComponent))
class WWISEMEDIARUNTIME_API UWwiseMediaAudioComponent : public UAkAudioInputComponent
{
    GENERATED_BODY()

public:
    UWwiseMediaAudioComponent(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia")
    TObjectPtr<UMediaPlayer> MediaPlayer;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WwiseMedia")
    bool bAutoPlay = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia|Buffering", meta=(ClampMin="0", ClampMax="100"))
    float PrebufferMilliseconds = 20;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia|Buffering", meta=(ClampMin="3", ClampMax="32"))
    int32 PcmBufferBlocks = 8;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia|Buffering", meta=(ClampMin="2", ClampMax="64"))
    int32 MaximumMediaPackets = 16;

    /** Calibration only; positive values account for downstream audio latency. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia|Synchronization", meta=(ClampMin="0", ClampMax="500"))
    float OutputLatencyMilliseconds = 0;

    /** A stalled/virtualized Audio Input must not silently deadlock the media clock. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WwiseMedia|Buffering", meta=(ClampMin="0.5", ClampMax="30"))
    float CallbackTimeoutSeconds = 3;

    UPROPERTY(BlueprintAssignable, Category="WwiseMedia")
    FWwiseMediaStateChanged OnStateChanged;

    UPROPERTY(BlueprintAssignable, Category="WwiseMedia")
    FWwiseMediaError OnPlaybackError;

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    void SetMediaPlayer(UMediaPlayer* Player);

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    bool OpenMediaSource(UMediaSource* Source, bool bPlayWhenOpened = true);

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    bool PlayMedia();

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    bool PauseMedia();

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    bool SeekMedia(FTimespan Time);

    UFUNCTION(BlueprintCallable, Category="WwiseMedia")
    void StopMedia(bool bClosePlayer = false);

    UFUNCTION(BlueprintPure, Category="WwiseMedia")
    FWwiseMediaDebugInfo GetDebugInfo() const;

    virtual int32 PostAssociatedAudioInputEvent() override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void PostUnregisterGameObject() override;
    virtual void Stop() override;
    virtual bool FillSamplesBuffer(uint32 Channels, uint32 Frames, float** Output) override;
    virtual void GetChannelConfig(AkAudioFormat& Format) override;

private:
    void BindPlayer();
    void UnbindPlayer();
    bool PrepareBridge();
    void StopAudioInput();
    void ChangeState(EWwiseMediaPlaybackState NewState);
    void Fail(const FString& Message);

    UFUNCTION()
    void HandleOpened(FString Url);
    UFUNCTION()
    void HandleOpenFailed(FString Url);
    UFUNCTION()
    void HandleClosed();
    UFUNCTION()
    void HandleEndReached();
    UFUNCTION()
    void HandleSeekCompleted();

    TWeakObjectPtr<UMediaPlayer> BoundPlayer;
    TSharedPtr<FWwiseMediaAudioBridge, ESPMode::ThreadSafe> Bridge;
    EWwiseMediaPlaybackState PlaybackState = EWwiseMediaPlaybackState::Closed;
    FString LastError;
    AkPlayingID ActivePlayingID = AK_INVALID_PLAYING_ID;
    double StartRequestedAt = 0;
    double EndReachedAt = 0;
    double TailDrainedAt = 0;
    double EventPostedAt = 0;
    bool bPlayWhenOpened = false;
    bool bResumeAfterSeek = false;
    bool bWaitingForSeek = false;
    bool bReachedEnd = false;
};
