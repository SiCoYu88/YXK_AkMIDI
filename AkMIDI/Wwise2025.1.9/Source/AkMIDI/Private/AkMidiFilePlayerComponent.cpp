#include "AkMidiFilePlayerComponent.h"

#include "AkAudioEvent.h"
#include "AkMidiComponent.h"
#include "AkMidiFileParser.h"
#include "AkMidiMessage.h"
#include "Engine/World.h"

UAkMidiFilePlayerComponent::UAkMidiFilePlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	ResetActiveNotes();
}

bool UAkMidiFilePlayerComponent::LoadMidiFile(const FString& FilePath)
{
	Stop(true);

	FString Error;
	FAkMidiFileData ParsedData;
	const EAkMidiFileParseResult Result = FAkMidiFileParser::ParseFile(FilePath, ParsedData, Error);
	if (Result != EAkMidiFileParseResult::Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("Load MIDI file failed: %s"), *Error);
		LoadedFileData = FAkMidiFileData();
		PlayerState = EAkMidiFilePlayerState::Unloaded;
		return false;
	}

	LoadedFileData = ParsedData;
	CurrentEventIndex = 0;
	CurrentPlaybackTimeSeconds = 0.0;
	ResetActiveNotes();
	PlayerState = EAkMidiFilePlayerState::Loaded;
	return true;
}

bool UAkMidiFilePlayerComponent::Play(UAkMidiComponent* InMidiComponent, UAkAudioEvent* InAkEvent)
{
	if (!InMidiComponent || LoadedFileData.Events.Num() == 0)
	{
		return false;
	}

	TargetMidiComponent = InMidiComponent;
	TargetAkEvent = InAkEvent;

	if (bAutoConfigureMidiComponent)
	{
		TargetMidiComponent->OpenMidiInputDevice(127);
		TargetMidiComponent->OpenMidiOutputDevice(127);
	}

	if (PlayerState == EAkMidiFilePlayerState::Finished || PlayerState == EAkMidiFilePlayerState::Stopped || CurrentPlaybackTimeSeconds >= LoadedFileData.DurationSeconds)
	{
		CurrentPlaybackTimeSeconds = 0.0;
		CurrentEventIndex = 0;
		ResetActiveNotes();
	}
	else
	{
		CurrentEventIndex = FindEventIndexAtTime(CurrentPlaybackTimeSeconds);
	}

	PlayerState = EAkMidiFilePlayerState::Playing;
	DispatchDueEvents();
	return true;
}

void UAkMidiFilePlayerComponent::Stop(bool bSendAllNotesOff)
{
	if (bSendAllNotesOff)
	{
		SendAllNotesOff();
	}

	CurrentPlaybackTimeSeconds = 0.0;
	CurrentEventIndex = 0;
	ResetActiveNotes();

	if (LoadedFileData.Events.Num() > 0)
	{
		PlayerState = EAkMidiFilePlayerState::Stopped;
	}
	else
	{
		PlayerState = EAkMidiFilePlayerState::Unloaded;
	}
}

void UAkMidiFilePlayerComponent::Pause()
{
	if (PlayerState != EAkMidiFilePlayerState::Playing)
	{
		return;
	}

	SendAllNotesOff();
	PlayerState = EAkMidiFilePlayerState::Paused;
}

void UAkMidiFilePlayerComponent::Resume()
{
	if (PlayerState != EAkMidiFilePlayerState::Paused || !TargetMidiComponent)
	{
		return;
	}

	CurrentEventIndex = FindEventIndexAtTime(CurrentPlaybackTimeSeconds);
	ResetActiveNotes();
	RecoverActiveNotesAtTime(CurrentPlaybackTimeSeconds);
	PlayerState = EAkMidiFilePlayerState::Playing;
	DispatchDueEvents();
}

bool UAkMidiFilePlayerComponent::SeekSeconds(double InSeconds)
{
	if (LoadedFileData.Events.Num() == 0)
	{
		return false;
	}

	SendAllNotesOff();
	CurrentPlaybackTimeSeconds = FMath::Clamp(InSeconds, 0.0, LoadedFileData.DurationSeconds);
	CurrentEventIndex = FindEventIndexAtTime(CurrentPlaybackTimeSeconds);
	ResetActiveNotes();
	RecoverActiveNotesAtTime(CurrentPlaybackTimeSeconds);

	if (PlayerState == EAkMidiFilePlayerState::Finished || PlayerState == EAkMidiFilePlayerState::Stopped || PlayerState == EAkMidiFilePlayerState::Unloaded)
	{
		PlayerState = EAkMidiFilePlayerState::Loaded;
	}

	return true;
}

void UAkMidiFilePlayerComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetActiveNotes();
}

void UAkMidiFilePlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop(true);
	Super::EndPlay(EndPlayReason);
}

void UAkMidiFilePlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PlayerState != EAkMidiFilePlayerState::Playing)
	{
		return;
	}

	CurrentPlaybackTimeSeconds += static_cast<double>(DeltaTime) * static_cast<double>(FMath::Max(PlaybackRate, 0.0f));
	DispatchDueEvents();

	if (CurrentEventIndex >= LoadedFileData.Events.Num() && CurrentPlaybackTimeSeconds >= LoadedFileData.DurationSeconds)
	{
		SendAllNotesOff();
		PlayerState = EAkMidiFilePlayerState::Finished;
	}
}

void UAkMidiFilePlayerComponent::DispatchDueEvents()
{
	if (!TargetMidiComponent || PlayerState != EAkMidiFilePlayerState::Playing)
	{
		return;
	}

	TArray<UAkMidiMessage*> Messages;
	Messages.Reserve(FMath::Min(MaxEventsPerTick, LoadedFileData.Events.Num() - CurrentEventIndex));

	int32 DispatchedCount = 0;
	while (CurrentEventIndex < LoadedFileData.Events.Num() && DispatchedCount < MaxEventsPerTick)
	{
		const FAkMidiTimedEvent& Event = LoadedFileData.Events[CurrentEventIndex];
		if (Event.TimeSeconds > CurrentPlaybackTimeSeconds)
		{
			break;
		}

		if (UAkMidiMessage* Message = CreateMessageFromTimedEvent(Event))
		{
			Messages.Add(Message);
			UpdateActiveNotes(Event);
		}

		++CurrentEventIndex;
		++DispatchedCount;
	}

	if (Messages.Num() > 0)
	{
		TargetMidiComponent->PostMidiEvent(Messages, TargetAkEvent);
	}
}

void UAkMidiFilePlayerComponent::ResetActiveNotes()
{
	FMemory::Memzero(ActiveNotes, sizeof(ActiveNotes));
}

void UAkMidiFilePlayerComponent::UpdateActiveNotes(const FAkMidiTimedEvent& Event)
{
	if (Event.Channel >= 16 || Event.Data01 >= 128)
	{
		return;
	}

	if (Event.Type == EAkMessageType::AMT_Note_On && Event.Data02 > 0)
	{
		ActiveNotes[Event.Channel][Event.Data01] = true;
	}
	else if (Event.Type == EAkMessageType::AMT_Note_Off || (Event.Type == EAkMessageType::AMT_Note_On && Event.Data02 == 0))
	{
		ActiveNotes[Event.Channel][Event.Data01] = false;
	}
}

void UAkMidiFilePlayerComponent::SendAllNotesOff()
{
	if (!TargetMidiComponent)
	{
		ResetActiveNotes();
		return;
	}

	TArray<UAkMidiMessage*> Messages;
	for (uint8 Channel = 0; Channel < 16; ++Channel)
	{
		for (uint8 Note = 0; Note < 128; ++Note)
		{
			if (!ActiveNotes[Channel][Note])
			{
				continue;
			}

			UAkMidiMessage* Message = NewObject<UAkMidiMessage>(this);
			if (!Message)
			{
				continue;
			}

			Message->NoteType = EAkMessageType::AMT_Note_Off;
			Message->Channel = Channel;
			Message->NoteOffset = 0;
			Message->Data01 = Note;
			Message->Data02 = 0;
			Messages.Add(Message);
		}
	}

	if (Messages.Num() > 0)
	{
		TargetMidiComponent->PostMidiEvent(Messages, TargetAkEvent);
	}

	ResetActiveNotes();
}

void UAkMidiFilePlayerComponent::RecoverActiveNotesAtTime(double InSeconds)
{
	if (!TargetMidiComponent || LoadedFileData.Events.Num() == 0)
	{
		return;
	}

	bool bNoteActive[16][128];
	FMemory::Memzero(bNoteActive, sizeof(bNoteActive));

	for (int32 i = LoadedFileData.Events.Num() - 1; i >= 0; --i)
	{
		const FAkMidiTimedEvent& Event = LoadedFileData.Events[i];
		if (Event.TimeSeconds >= InSeconds)
		{
			continue;
		}

		if (Event.Channel >= 16 || Event.Data01 >= 128)
		{
			continue;
		}

		if (Event.Type == EAkMessageType::AMT_Note_Off || (Event.Type == EAkMessageType::AMT_Note_On && Event.Data02 == 0))
		{
			bNoteActive[Event.Channel][Event.Data01] = false;
		}
		else if (Event.Type == EAkMessageType::AMT_Note_On && Event.Data02 > 0)
		{
			if (!bNoteActive[Event.Channel][Event.Data01])
			{
				bNoteActive[Event.Channel][Event.Data01] = true;
				ActiveNotes[Event.Channel][Event.Data01] = true;

				UAkMidiMessage* Message = NewObject<UAkMidiMessage>(this);
				if (Message)
				{
					Message->NoteType = EAkMessageType::AMT_Note_On;
					Message->Channel = Event.Channel;
					Message->NoteOffset = 0;
					Message->Data01 = Event.Data01;
					Message->Data02 = Event.Data02;
					TArray<UAkMidiMessage*> Messages;
					Messages.Add(Message);
					TargetMidiComponent->PostMidiEvent(Messages, TargetAkEvent);
				}
			}
		}
	}
}

UAkMidiMessage* UAkMidiFilePlayerComponent::CreateMessageFromTimedEvent(const FAkMidiTimedEvent& Event)
{
	UAkMidiMessage* Message = NewObject<UAkMidiMessage>(this);
	if (!Message)
	{
		return nullptr;
	}

	Message->NoteType = Event.Type;
	Message->Channel = FMath::Clamp<uint8>(Event.Channel, 0, 15);
	Message->NoteOffset = 0;
	Message->Data01 = FMath::Clamp<uint8>(Event.Data01, 0, 127);
	Message->Data02 = FMath::Clamp<uint8>(Event.Data02, 0, 127);
	return Message;
}

int32 UAkMidiFilePlayerComponent::FindEventIndexAtTime(double InSeconds) const
{
	int32 Lower = 0;
	int32 Upper = LoadedFileData.Events.Num();

	while (Lower < Upper)
	{
		const int32 Middle = Lower + (Upper - Lower) / 2;
		if (LoadedFileData.Events[Middle].TimeSeconds < InSeconds)
		{
			Lower = Middle + 1;
		}
		else
		{
			Upper = Middle;
		}
	}

	return Lower;
}
