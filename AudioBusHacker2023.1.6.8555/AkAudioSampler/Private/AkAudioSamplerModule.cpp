// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AkAudioSamplerModule.h"
#include "Interfaces/IPluginManager.h"

IMPLEMENT_MODULE(FAkAudioSamplerModule, AkAudioSampler)

FAkAudioSamplerModule* FAkAudioSamplerModule::AkAudioSamplerModuleIntance = nullptr;

SABH FAkAudioSamplerModule::SetCallBackFunc = NULL;

void FAkAudioSamplerModule::StartupModule()
{
	
	//// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	////if (AkAudioSamplerModuleIntance)
	////{
	////	return;
	////}
    #if PLATFORM_64BITS
		//UE_LOG(LogTemp, Log, TEXT("FIND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
		FString platform = TEXT("x64_vc170/Profile/bin/");

		FString path = IPluginManager::Get().FindPlugin("Wwise")->GetBaseDir();
		FString dllpath = path + "/ThirdParty/" + platform + "AudioBusHacker.dll";
		UE_LOG(LogTemp, Log, TEXT("%s"),*dllpath);
		auto PdfDllHandle = FPlatformProcess::GetDllHandle(*dllpath);
		if (PdfDllHandle)
		{
			SetCallBackFunc = (SABH)FPlatformProcess::GetDllExport(PdfDllHandle, TEXT("SetAudioBusHackerCallbacks")); // Export the DLL function.
			if (SetCallBackFunc != NULL) {
			}
		}
	#endif
	AkAudioSamplerModuleIntance = this;
}

void FAkAudioSamplerModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	AkAudioSamplerModuleIntance = nullptr;
}


