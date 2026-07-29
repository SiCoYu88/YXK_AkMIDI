#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct FWwisePixelStreaming2Stats
{
	uint64 CapturedBuffers = 0;
	uint64 PushedBuffers = 0;
	uint64 DroppedBuffers = 0;
	uint64 RejectedBuffers = 0;
};

class WWISEPIXELSTREAMING2_API IWwisePixelStreaming2Module : public IModuleInterface
{
public:
	static IWwisePixelStreaming2Module& Get()
	{
		return FModuleManager::LoadModuleChecked<IWwisePixelStreaming2Module>(TEXT("WwisePixelStreaming2"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("WwisePixelStreaming2"));
	}

	virtual FWwisePixelStreaming2Stats GetStats() const = 0;
};
