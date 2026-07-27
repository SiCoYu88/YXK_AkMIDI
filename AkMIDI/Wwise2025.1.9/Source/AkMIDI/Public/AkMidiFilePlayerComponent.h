#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AkMidiFileTypes.h"
#include "AkMidiFilePlayerComponent.generated.h"

class UAkAudioEvent;
class UAkMidiComponent;
class UAkMidiMessage;

UCLASS(Blueprintable, BlueprintType, ClassGroup = "AkMIDI", meta = (BlueprintSpawnableComponent))
class AKMIDI_API UAkMidiFilePlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAkMidiFilePlayerComponent();

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	bool LoadMidiFile(const FString& FilePath);

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	bool Play(UAkMidiComponent* InMidiComponent, UAkAudioEvent* InAkEvent = nullptr);

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	void Stop(bool bSendAllNotesOff = true);

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	void Resume();

	UFUNCTION(BlueprintCallable, Category = "AkMIDI|MidiFile")
	bool SeekSeconds(double InSeconds);

	UFUNCTION(BlueprintPure, Category = "AkMIDI|MidiFile")
	double GetPlaybackTimeSeconds() const { return CurrentPlaybackTimeSeconds; }

	UFUNCTION(BlueprintPure, Category = "AkMIDI|MidiFile")
	double GetDurationSeconds() const { return LoadedFileData.DurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "AkMIDI|MidiFile")
	EAkMidiFilePlayerState GetPlayerState() const { return PlayerState; }

	UFUNCTION(BlueprintPure, Category = "AkMIDI|MidiFile")
	FAkMidiFileData GetLoadedFileData() const { return LoadedFileData; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|MidiFile", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PlaybackRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|MidiFile", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 MaxEventsPerTick = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|MidiFile")
	bool bAutoConfigureMidiComponent = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	FAkMidiFileData LoadedFileData;

	UPROPERTY(Transient)
	TObjectPtr<UAkMidiComponent> TargetMidiComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAkAudioEvent> TargetAkEvent = nullptr;

	UPROPERTY(Transient)
	EAkMidiFilePlayerState PlayerState = EAkMidiFilePlayerState::Unloaded;

	int32 CurrentEventIndex = 0;
	double CurrentPlaybackTimeSeconds = 0.0;
	bool ActiveNotes[16][128];

	void DispatchDueEvents();
	void ResetActiveNotes();
	void UpdateActiveNotes(const FAkMidiTimedEvent& Event);
	void SendAllNotesOff();
	void RecoverActiveNotesAtTime(double InSeconds);
	UAkMidiMessage* CreateMessageFromTimedEvent(const FAkMidiTimedEvent& Event);
	int32 FindEventIndexAtTime(double InSeconds) const;
};
