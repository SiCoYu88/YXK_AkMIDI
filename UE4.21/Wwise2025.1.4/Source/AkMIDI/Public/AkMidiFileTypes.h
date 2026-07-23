#pragma once

#include "CoreMinimal.h"
#include "AkMidiMessage.h"
#include "AkMidiFileTypes.generated.h"

UENUM(BlueprintType)
enum class EAkMidiFileParseResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	FileNotFound UMETA(DisplayName = "File Not Found"),
	FileTooLarge UMETA(DisplayName = "File Too Large"),
	InvalidHeader UMETA(DisplayName = "Invalid Header"),
	UnsupportedFormat UMETA(DisplayName = "Unsupported Format"),
	UnsupportedDivision UMETA(DisplayName = "Unsupported Division"),
	TruncatedChunk UMETA(DisplayName = "Truncated Chunk"),
	InvalidVLQ UMETA(DisplayName = "Invalid VLQ"),
	InvalidRunningStatus UMETA(DisplayName = "Invalid Running Status"),
	UnknownError UMETA(DisplayName = "Unknown Error")
};

UENUM(BlueprintType)
enum class EAkMidiFilePlayerState : uint8
{
	Unloaded UMETA(DisplayName = "Unloaded"),
	Loaded UMETA(DisplayName = "Loaded"),
	Playing UMETA(DisplayName = "Playing"),
	Paused UMETA(DisplayName = "Paused"),
	Stopped UMETA(DisplayName = "Stopped"),
	Finished UMETA(DisplayName = "Finished")
};

USTRUCT(BlueprintType)
struct AKMIDI_API FAkMidiTimedEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	int64 Tick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	double TimeSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	EAkMessageType Type = EAkMessageType::AMT_Note_On;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	uint8 Channel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	uint8 Data01 = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	uint8 Data02 = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	int32 TrackIndex = 0;
};

USTRUCT(BlueprintType)
struct AKMIDI_API FAkMidiFileData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	int32 Format = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	int32 TrackCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	int32 TicksPerQuarterNote = 480;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	double DurationSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|MidiFile")
	TArray<FAkMidiTimedEvent> Events;
};
