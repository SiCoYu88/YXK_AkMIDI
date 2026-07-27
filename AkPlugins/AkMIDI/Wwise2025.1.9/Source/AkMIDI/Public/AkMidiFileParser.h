#pragma once

#include "CoreMinimal.h"
#include "AkMidiFileTypes.h"

class AKMIDI_API FAkMidiFileParser
{
public:
	static EAkMidiFileParseResult ParseFile(const FString& FilePath, FAkMidiFileData& OutData, FString& OutError);
	static EAkMidiFileParseResult ParseBytes(const TArray<uint8>& Bytes, FAkMidiFileData& OutData, FString& OutError);
};
