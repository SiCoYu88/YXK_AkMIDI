#include "AkMidiFileParser.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int64 MaxMidiFileSize = 16 * 1024 * 1024;
	constexpr int32 DefaultTempoMicrosecondsPerQuarter = 500000;

	struct FParsedMidiEvent
	{
		int64 Tick = 0;
		EAkMessageType Type = EAkMessageType::AMT_Note_On;
		uint8 Channel = 0;
		uint8 Data01 = 0;
		uint8 Data02 = 0;
		int32 TrackIndex = 0;
	};

	struct FTempoEvent
	{
		int64 Tick = 0;
		int32 MicrosecondsPerQuarter = DefaultTempoMicrosecondsPerQuarter;
		int32 Order = 0;
	};

	static bool ReadUInt16BE(const TArray<uint8>& Bytes, int64& Pos, int64 End, uint16& OutValue)
	{
		if (Pos + 2 > End)
		{
			return false;
		}

		OutValue = (static_cast<uint16>(Bytes[Pos]) << 8) | static_cast<uint16>(Bytes[Pos + 1]);
		Pos += 2;
		return true;
	}

	static bool ReadUInt32BE(const TArray<uint8>& Bytes, int64& Pos, int64 End, uint32& OutValue)
	{
		if (Pos + 4 > End)
		{
			return false;
		}

		OutValue = (static_cast<uint32>(Bytes[Pos]) << 24)
			| (static_cast<uint32>(Bytes[Pos + 1]) << 16)
			| (static_cast<uint32>(Bytes[Pos + 2]) << 8)
			| static_cast<uint32>(Bytes[Pos + 3]);
		Pos += 4;
		return true;
	}

	static bool ReadChunkId(const TArray<uint8>& Bytes, int64& Pos, int64 End, char OutId[5])
	{
		if (Pos + 4 > End)
		{
			return false;
		}

		OutId[0] = static_cast<char>(Bytes[Pos]);
		OutId[1] = static_cast<char>(Bytes[Pos + 1]);
		OutId[2] = static_cast<char>(Bytes[Pos + 2]);
		OutId[3] = static_cast<char>(Bytes[Pos + 3]);
		OutId[4] = '\0';
		Pos += 4;
		return true;
	}

	static bool ReadVLQ(const TArray<uint8>& Bytes, int64& Pos, int64 End, int64& OutValue)
	{
		OutValue = 0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Pos >= End)
			{
				return false;
			}

			const uint8 Byte = Bytes[Pos++];
			OutValue = (OutValue << 7) | (Byte & 0x7F);
			if ((Byte & 0x80) == 0)
			{
				return true;
			}
		}

		return false;
	}

	static int32 GetSystemMessageDataLength(uint8 Status)
	{
		switch (Status)
		{
		case 0xF1:
		case 0xF3:
			return 1;
		case 0xF2:
			return 2;
		case 0xF6:
		case 0xF8:
		case 0xFA:
		case 0xFB:
		case 0xFC:
		case 0xFE:
			return 0;
		default:
			return -1;
		}
	}

	static bool MapChannelEvent(uint8 Status, uint8 Data01, uint8 Data02, int64 Tick, int32 TrackIndex, FParsedMidiEvent& OutEvent)
	{
		const uint8 Type = Status & 0xF0;
		OutEvent.Tick = Tick;
		OutEvent.Channel = Status & 0x0F;
		OutEvent.Data01 = Data01;
		OutEvent.Data02 = Data02;
		OutEvent.TrackIndex = TrackIndex;

		switch (Type)
		{
		case 0x80:
			OutEvent.Type = EAkMessageType::AMT_Note_Off;
			return true;
		case 0x90:
			OutEvent.Type = Data02 == 0 ? EAkMessageType::AMT_Note_Off : EAkMessageType::AMT_Note_On;
			return true;
		case 0xA0:
			OutEvent.Type = EAkMessageType::AMT_AfterTouch;
			return true;
		case 0xB0:
			OutEvent.Type = EAkMessageType::AMT_CC;
			return true;
		case 0xC0:
			OutEvent.Type = EAkMessageType::AMT_Program_Change;
			OutEvent.Data02 = 0;
			return true;
		case 0xD0:
			OutEvent.Type = EAkMessageType::AMT_Channel_AfterTouch;
			OutEvent.Data02 = 0;
			return true;
		case 0xE0:
			OutEvent.Type = EAkMessageType::AMT_Pitch_Bend;
			return true;
		default:
			return false;
		}
	}

	static double TickToSeconds(int64 TargetTick, const TArray<FTempoEvent>& TempoEvents, int32 TicksPerQuarterNote)
	{
		double Seconds = 0.0;
		int64 CurrentTick = 0;
		int32 CurrentTempo = DefaultTempoMicrosecondsPerQuarter;

		for (const FTempoEvent& TempoEvent : TempoEvents)
		{
			if (TempoEvent.Tick > TargetTick)
			{
				break;
			}

			if (TempoEvent.Tick > CurrentTick)
			{
				Seconds += static_cast<double>(TempoEvent.Tick - CurrentTick) * static_cast<double>(CurrentTempo) / 1000000.0 / static_cast<double>(TicksPerQuarterNote);
				CurrentTick = TempoEvent.Tick;
			}

			CurrentTempo = TempoEvent.MicrosecondsPerQuarter;
		}

		if (TargetTick > CurrentTick)
		{
			Seconds += static_cast<double>(TargetTick - CurrentTick) * static_cast<double>(CurrentTempo) / 1000000.0 / static_cast<double>(TicksPerQuarterNote);
		}

		return Seconds;
	}

	static EAkMidiFileParseResult ParseTrack(const TArray<uint8>& Bytes, int64 TrackStart, int64 TrackEnd, int32 TrackIndex, TArray<FParsedMidiEvent>& OutEvents, TArray<FTempoEvent>& OutTempoEvents, FString& OutError)
	{
		int64 Pos = TrackStart;
		int64 AbsoluteTick = 0;
		uint8 RunningStatus = 0;

		while (Pos < TrackEnd)
		{
			int64 DeltaTicks = 0;
			if (!ReadVLQ(Bytes, Pos, TrackEnd, DeltaTicks))
			{
				OutError = TEXT("Invalid delta-time VLQ in MIDI track.");
				return EAkMidiFileParseResult::InvalidVLQ;
			}

			AbsoluteTick += DeltaTicks;
			if (Pos >= TrackEnd)
			{
				OutError = TEXT("Unexpected end of track after delta-time.");
				return EAkMidiFileParseResult::TruncatedChunk;
			}

			uint8 Status = Bytes[Pos];
			const bool bUsesRunningStatus = Status < 0x80;
			if (bUsesRunningStatus)
			{
				if (RunningStatus == 0)
				{
					OutError = TEXT("MIDI event uses running status before any channel status byte.");
					return EAkMidiFileParseResult::InvalidRunningStatus;
				}

				Status = RunningStatus;
			}
			else
			{
				++Pos;
			}

			if (Status == 0xFF)
			{
				if (bUsesRunningStatus || Pos >= TrackEnd)
				{
					OutError = TEXT("Invalid MIDI meta event.");
					return EAkMidiFileParseResult::TruncatedChunk;
				}

				const uint8 MetaType = Bytes[Pos++];
				int64 MetaLength = 0;
				if (!ReadVLQ(Bytes, Pos, TrackEnd, MetaLength))
				{
					OutError = TEXT("Invalid meta event length VLQ.");
					return EAkMidiFileParseResult::InvalidVLQ;
				}

				if (Pos + MetaLength > TrackEnd)
				{
					OutError = TEXT("Meta event exceeds track boundary.");
					return EAkMidiFileParseResult::TruncatedChunk;
				}

				if (MetaType == 0x51 && MetaLength == 3)
				{
					const int32 Tempo = (static_cast<int32>(Bytes[Pos]) << 16)
						| (static_cast<int32>(Bytes[Pos + 1]) << 8)
						| static_cast<int32>(Bytes[Pos + 2]);
					if (Tempo > 0)
					{
						FTempoEvent TempoEvent;
						TempoEvent.Tick = AbsoluteTick;
						TempoEvent.MicrosecondsPerQuarter = Tempo;
						TempoEvent.Order = OutTempoEvents.Num();
						OutTempoEvents.Add(TempoEvent);
					}
				}

				Pos += MetaLength;
				if (MetaType == 0x2F)
				{
					break;
				}

				continue;
			}

			if (Status == 0xF0 || Status == 0xF7)
			{
				int64 SysExLength = 0;
				if (!ReadVLQ(Bytes, Pos, TrackEnd, SysExLength))
				{
					OutError = TEXT("Invalid SysEx length VLQ.");
					return EAkMidiFileParseResult::InvalidVLQ;
				}

				if (Pos + SysExLength > TrackEnd)
				{
					OutError = TEXT("SysEx event exceeds track boundary.");
					return EAkMidiFileParseResult::TruncatedChunk;
				}

				Pos += SysExLength;
				continue;
			}

			if ((Status & 0xF0) >= 0x80 && (Status & 0xF0) <= 0xE0)
			{
				RunningStatus = Status;
				const bool bOneByteMessage = (Status & 0xF0) == 0xC0 || (Status & 0xF0) == 0xD0;
				const int32 DataLength = bOneByteMessage ? 1 : 2;
				if (Pos + DataLength > TrackEnd)
				{
					OutError = TEXT("MIDI channel event exceeds track boundary.");
					return EAkMidiFileParseResult::TruncatedChunk;
				}

				const uint8 Data01 = Bytes[Pos++];
				const uint8 Data02 = bOneByteMessage ? 0 : Bytes[Pos++];

				FParsedMidiEvent ParsedEvent;
				if (MapChannelEvent(Status, Data01, Data02, AbsoluteTick, TrackIndex, ParsedEvent))
				{
					OutEvents.Add(ParsedEvent);
				}
				continue;
			}

			const int32 SystemDataLength = GetSystemMessageDataLength(Status);
			if (SystemDataLength >= 0)
			{
				if (Pos + SystemDataLength > TrackEnd)
				{
					OutError = TEXT("MIDI system event exceeds track boundary.");
					return EAkMidiFileParseResult::TruncatedChunk;
				}

				Pos += SystemDataLength;
				continue;
			}

			OutError = FString::Printf(TEXT("Unsupported MIDI status byte 0x%02X."), Status);
			return EAkMidiFileParseResult::UnknownError;
		}

		return EAkMidiFileParseResult::Success;
	}
}

EAkMidiFileParseResult FAkMidiFileParser::ParseFile(const FString& FilePath, FAkMidiFileData& OutData, FString& OutError)
{
	OutData = FAkMidiFileData();
	OutError.Reset();

	if (!FPaths::FileExists(FilePath))
	{
		OutError = FString::Printf(TEXT("MIDI file not found: %s"), *FilePath);
		return EAkMidiFileParseResult::FileNotFound;
	}

	const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	if (FileSize < 0)
	{
		OutError = FString::Printf(TEXT("Unable to query MIDI file size: %s"), *FilePath);
		return EAkMidiFileParseResult::FileNotFound;
	}

	if (FileSize > MaxMidiFileSize)
	{
		OutError = FString::Printf(TEXT("MIDI file is too large: %lld bytes."), FileSize);
		return EAkMidiFileParseResult::FileTooLarge;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		OutError = FString::Printf(TEXT("Unable to read MIDI file: %s"), *FilePath);
		return EAkMidiFileParseResult::FileNotFound;
	}

	return ParseBytes(Bytes, OutData, OutError);
}

EAkMidiFileParseResult FAkMidiFileParser::ParseBytes(const TArray<uint8>& Bytes, FAkMidiFileData& OutData, FString& OutError)
{
	OutData = FAkMidiFileData();
	OutError.Reset();

	const int64 End = Bytes.Num();
	int64 Pos = 0;

	char ChunkId[5];
	uint32 HeaderLength = 0;
	if (!ReadChunkId(Bytes, Pos, End, ChunkId) || FCStringAnsi::Strcmp(ChunkId, "MThd") != 0 || !ReadUInt32BE(Bytes, Pos, End, HeaderLength))
	{
		OutError = TEXT("Invalid MIDI header chunk.");
		return EAkMidiFileParseResult::InvalidHeader;
	}

	if (HeaderLength < 6 || Pos + HeaderLength > End)
	{
		OutError = TEXT("Invalid MIDI header length.");
		return EAkMidiFileParseResult::InvalidHeader;
	}

	const int64 HeaderEnd = Pos + HeaderLength;
	uint16 Format = 0;
	uint16 TrackCount = 0;
	uint16 Division = 0;
	if (!ReadUInt16BE(Bytes, Pos, HeaderEnd, Format) || !ReadUInt16BE(Bytes, Pos, HeaderEnd, TrackCount) || !ReadUInt16BE(Bytes, Pos, HeaderEnd, Division))
	{
		OutError = TEXT("Truncated MIDI header.");
		return EAkMidiFileParseResult::InvalidHeader;
	}

	Pos = HeaderEnd;

	if (Format > 1)
	{
		OutError = FString::Printf(TEXT("Unsupported MIDI format: %d."), Format);
		return EAkMidiFileParseResult::UnsupportedFormat;
	}

	if ((Division & 0x8000) != 0 || Division == 0)
	{
		OutError = TEXT("Only PPQ MIDI time division is supported.");
		return EAkMidiFileParseResult::UnsupportedDivision;
	}

	TArray<FParsedMidiEvent> ParsedEvents;
	TArray<FTempoEvent> TempoEvents;
	TempoEvents.Add(FTempoEvent());

	for (int32 TrackIndex = 0; TrackIndex < TrackCount; ++TrackIndex)
	{
		if (!ReadChunkId(Bytes, Pos, End, ChunkId))
		{
			OutError = TEXT("Missing MIDI track chunk.");
			return EAkMidiFileParseResult::TruncatedChunk;
		}

		uint32 TrackLength = 0;
		if (!ReadUInt32BE(Bytes, Pos, End, TrackLength))
		{
			OutError = TEXT("Missing MIDI track length.");
			return EAkMidiFileParseResult::TruncatedChunk;
		}

		if (Pos + TrackLength > End)
		{
			OutError = TEXT("MIDI track chunk exceeds file size.");
			return EAkMidiFileParseResult::TruncatedChunk;
		}

		const int64 TrackStart = Pos;
		const int64 TrackEnd = Pos + TrackLength;
		Pos = TrackEnd;

		if (FCStringAnsi::Strcmp(ChunkId, "MTrk") != 0)
		{
			--TrackIndex;
			continue;
		}

		const EAkMidiFileParseResult TrackResult = ParseTrack(Bytes, TrackStart, TrackEnd, TrackIndex, ParsedEvents, TempoEvents, OutError);
		if (TrackResult != EAkMidiFileParseResult::Success)
		{
			return TrackResult;
		}
	}

	TempoEvents.Sort([](const FTempoEvent& A, const FTempoEvent& B)
	{
		if (A.Tick == B.Tick)
		{
			return A.Order < B.Order;
		}
		return A.Tick < B.Tick;
	});

	ParsedEvents.Sort([](const FParsedMidiEvent& A, const FParsedMidiEvent& B)
	{
		if (A.Tick == B.Tick)
		{
			return A.TrackIndex < B.TrackIndex;
		}
		return A.Tick < B.Tick;
	});

	OutData.Format = Format;
	OutData.TrackCount = TrackCount;
	OutData.TicksPerQuarterNote = Division;
	OutData.Events.Reserve(ParsedEvents.Num());

	for (const FParsedMidiEvent& ParsedEvent : ParsedEvents)
	{
		FAkMidiTimedEvent TimedEvent;
		TimedEvent.Tick = ParsedEvent.Tick;
		TimedEvent.TimeSeconds = TickToSeconds(ParsedEvent.Tick, TempoEvents, Division);
		TimedEvent.Type = ParsedEvent.Type;
		TimedEvent.Channel = ParsedEvent.Channel;
		TimedEvent.Data01 = ParsedEvent.Data01;
		TimedEvent.Data02 = ParsedEvent.Data02;
		TimedEvent.TrackIndex = ParsedEvent.TrackIndex;
		OutData.DurationSeconds = FMath::Max(OutData.DurationSeconds, TimedEvent.TimeSeconds);
		OutData.Events.Add(TimedEvent);
	}

	return EAkMidiFileParseResult::Success;
}
