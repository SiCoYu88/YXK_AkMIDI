// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AkAudioDevice.h"
#include "Ak/Plugin/AkAudioBusHackerPlugin.h"

typedef int(*SABH)(AkAudioBusHackerPluginExecuteCallbackFunc ABHfnc);

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
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	static SABH SetCallBackFunc;
	static FAkAudioSamplerModule* AkAudioSamplerModuleIntance;
};

