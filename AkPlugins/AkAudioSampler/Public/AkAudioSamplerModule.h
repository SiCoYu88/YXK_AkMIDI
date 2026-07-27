// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AkAudioDevice.h"
#define AUDIO_BUS_HACKER_NO_STATIC_LINK
#include "Ak/Plugin/AudioBusHackerFXFactory.h"
#undef AUDIO_BUS_HACKER_NO_STATIC_LINK

using FSetAudioBusHackerVisualizationCallback = int(*)(AkAudioBusHackerVisualizationCallbackFunc);

class IAkAudioSamplerModule : public IModuleInterface
{
public:
	static inline IAkAudioSamplerModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IAkAudioSamplerModule>("AkAudioSampler");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AkAudioSampler");
	}
};

class FAkAudioSamplerModule : public IAkAudioSamplerModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static int32 SetVisualizationCallback(AkAudioBusHackerVisualizationCallbackFunc InCallback);
	static bool IsVisualizationCallbackAvailable();

private:
	static FSetAudioBusHackerVisualizationCallback SetVisualizationCallbackFunc;
	static void* AudioBusHackerDllHandle;
};
