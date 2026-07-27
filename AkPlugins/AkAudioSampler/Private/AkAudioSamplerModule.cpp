// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AkAudioSamplerModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

IMPLEMENT_MODULE(FAkAudioSamplerModule, AkAudioSampler)

DEFINE_LOG_CATEGORY_STATIC(LogAkAudioSampler, Log, All);

#if PLATFORM_ANDROID
AK_STATIC_LINK_PLUGIN(AudioBusHackerFX);
#endif

FSetAudioBusHackerVisualizationCallback FAkAudioSamplerModule::SetVisualizationCallbackFunc = nullptr;
void* FAkAudioSamplerModule::AudioBusHackerDllHandle = nullptr;

void FAkAudioSamplerModule::StartupModule()
{
#if PLATFORM_WINDOWS
	const TSharedPtr<IPlugin> WwisePlugin = IPluginManager::Get().FindPlugin(TEXT("Wwise"));
	if (!WwisePlugin.IsValid())
	{
		UE_LOG(LogAkAudioSampler, Error, TEXT("Unable to locate the Wwise plugin."));
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
			WwisePlugin->GetBaseDir(),
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
			TEXT("AudioBusHacker.dll with the visualization API was not found in Wwise/ThirdParty."));
	}
#endif
}

void FAkAudioSamplerModule::ShutdownModule()
{
	if (IsVisualizationCallbackAvailable())
	{
		SetVisualizationCallback(nullptr);
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
