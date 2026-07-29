#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct FAkWwisePixelStreamingStats
{
	uint64 CapturedBuffers = 0;
	uint64 PushedBuffers = 0;
	uint64 DroppedBuffers = 0;
	uint64 RejectedBuffers = 0;
};

class AKWWISEPIXELSTREAMING_API IAkWwisePixelStreamingModule : public IModuleInterface
{
public:
	static IAkWwisePixelStreamingModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IAkWwisePixelStreamingModule>(TEXT("AkWwisePixelStreaming"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("AkWwisePixelStreaming"));
	}

	virtual FAkWwisePixelStreamingStats GetStats() const = 0;
};
