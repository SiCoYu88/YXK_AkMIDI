#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct FWwisePixelStreaming2Stats
{
	uint64 CapturedBuffers = 0;
	uint64 PushedBuffers = 0;
	uint64 DroppedBuffers = 0;
	uint64 RejectedBuffers = 0;
	uint64 NonSilentBuffers = 0;
	float LastPeak = 0.0f;
	float MaxPeak = 0.0f;
	int32 LastNumFrames = 0;
	int32 LastNumChannels = 0;
	int32 LastSampleRate = 0;
	bool bCaptureRegistered = false;
	bool bStreamerAttached = false;
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
	virtual void LogStatus() const = 0;
};
