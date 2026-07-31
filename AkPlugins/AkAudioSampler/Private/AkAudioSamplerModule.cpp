// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AkAudioSamplerModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

IMPLEMENT_MODULE(FAkAudioSamplerModule, AkAudioSampler)

DEFINE_LOG_CATEGORY_STATIC(LogAkAudioSampler, Log, All);

#if PLATFORM_ANDROID
AK_STATIC_LINK_PLUGIN(AudioBusHackerFX);
#endif

FSetAudioBusHackerVisualizationCallback FAkAudioSamplerModule::SetVisualizationCallbackFunc = nullptr;
void* FAkAudioSamplerModule::AudioBusHackerDllHandle = nullptr;
FCriticalSection FAkAudioSamplerModule::VisualizationCallbackMutex;
AkAudioBusHackerVisualizationCallbackFunc FAkAudioSamplerModule::RegisteredVisualizationCallback = nullptr;
int32 FAkAudioSamplerModule::VisualizationConsumerCount = 0;
int32 FAkAudioSamplerModule::LastVisualizationCallbackResult = 1;

void FAkAudioSamplerModule::StartupModule()
{
#if PLATFORM_WINDOWS
	const TSharedPtr<IPlugin> WwiseSoundEnginePlugin = IPluginManager::Get().FindPlugin(TEXT("WwiseSoundEngine"));
	if (!WwiseSoundEnginePlugin.IsValid())
	{
		UE_LOG(LogAkAudioSampler, Error, TEXT("Unable to locate the WwiseSoundEngine plugin."));
		return;
	}

#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	const TCHAR* PreferredConfiguration = TEXT("Release");
#elif UE_BUILD_DEBUG
	const TCHAR* PreferredConfiguration = TEXT("Debug");
#else
	const TCHAR* PreferredConfiguration = TEXT("Profile");
#endif

	const TCHAR* Configurations[] =
	{
		PreferredConfiguration,
		TEXT("Profile"),
		TEXT("Release"),
		TEXT("Debug")
	};

	for (const TCHAR* Configuration : Configurations)
	{
		const FString DllPath = FPaths::Combine(
			WwiseSoundEnginePlugin->GetBaseDir(),
			TEXT("ThirdParty"),
			TEXT("x64_vc170"),
			Configuration,
			TEXT("bin"),
			TEXT("AudioBusHacker.dll"));
		if (!IFileManager::Get().FileExists(*DllPath))
		{
			continue;
		}

		void* CandidateHandle = FPlatformProcess::GetDllHandle(*DllPath);
		if (!CandidateHandle)
		{
			continue;
		}

		FSetAudioBusHackerVisualizationCallback CandidateFunction =
			reinterpret_cast<FSetAudioBusHackerVisualizationCallback>(
				FPlatformProcess::GetDllExport(
					CandidateHandle,
					TEXT("SetAudioBusHackerVisualizationCallback")));
		if (!CandidateFunction)
		{
			FPlatformProcess::FreeDllHandle(CandidateHandle);
			continue;
		}

		AudioBusHackerDllHandle = CandidateHandle;
		SetVisualizationCallbackFunc = CandidateFunction;
		UE_LOG(LogAkAudioSampler, Log, TEXT("Loaded AudioBusHacker visualization API from %s"), *DllPath);
		break;
	}

	if (!SetVisualizationCallbackFunc)
	{
		UE_LOG(
			LogAkAudioSampler,
			Error,
			TEXT("AudioBusHacker.dll with the visualization API was not found in WwiseSoundEngine/ThirdParty."));
	}
#endif
}

void FAkAudioSamplerModule::ShutdownModule()
{
	{
		FScopeLock Lock(&VisualizationCallbackMutex);
		if (IsVisualizationCallbackAvailable())
		{
			SetVisualizationCallback(nullptr);
		}
		RegisteredVisualizationCallback = nullptr;
		VisualizationConsumerCount = 0;
		LastVisualizationCallbackResult = 1;
	}
	SetVisualizationCallbackFunc = nullptr;

	if (AudioBusHackerDllHandle)
	{
		FPlatformProcess::FreeDllHandle(AudioBusHackerDllHandle);
		AudioBusHackerDllHandle = nullptr;
	}
}

int32 FAkAudioSamplerModule::SetVisualizationCallback(
	AkAudioBusHackerVisualizationCallbackFunc InCallback)
{
#if PLATFORM_WINDOWS
	return SetVisualizationCallbackFunc ? SetVisualizationCallbackFunc(InCallback) : 1;
#elif PLATFORM_ANDROID
	return SetAudioBusHackerVisualizationCallback(InCallback);
#else
	return 1;
#endif
}

bool FAkAudioSamplerModule::IsVisualizationCallbackAvailable()
{
#if PLATFORM_WINDOWS
	return SetVisualizationCallbackFunc != nullptr;
#elif PLATFORM_ANDROID
	return true;
#else
	return false;
#endif
}

bool FAkAudioSamplerModule::AcquireVisualizationCallback(
	AkAudioBusHackerVisualizationCallbackFunc InCallback,
	int32* OutResult)
{
	FScopeLock Lock(&VisualizationCallbackMutex);

	if (!InCallback || !IsVisualizationCallbackAvailable())
	{
		if (OutResult)
		{
			*OutResult = 1;
		}
		return false;
	}

	if (VisualizationConsumerCount == 0)
	{
		LastVisualizationCallbackResult = SetVisualizationCallback(InCallback);
		RegisteredVisualizationCallback = InCallback;
	}
	else if (RegisteredVisualizationCallback != InCallback)
	{
		UE_LOG(
			LogAkAudioSampler,
			Error,
			TEXT("AudioBusHacker supports only one visualization callback per process."));
		if (OutResult)
		{
			*OutResult = 1;
		}
		return false;
	}

	++VisualizationConsumerCount;
	if (OutResult)
	{
		*OutResult = LastVisualizationCallbackResult;
	}
	return true;
}

int32 FAkAudioSamplerModule::ReleaseVisualizationCallback()
{
	FScopeLock Lock(&VisualizationCallbackMutex);

	if (VisualizationConsumerCount <= 0)
	{
		return 1;
	}

	--VisualizationConsumerCount;
	if (VisualizationConsumerCount == 0)
	{
		LastVisualizationCallbackResult = SetVisualizationCallback(nullptr);
		RegisteredVisualizationCallback = nullptr;
	}

	return LastVisualizationCallbackResult;
}
