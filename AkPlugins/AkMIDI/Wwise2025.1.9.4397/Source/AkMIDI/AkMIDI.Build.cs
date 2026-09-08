#region H3D
// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class AkMIDI : ModuleRules
{
    public AkMIDI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Classes"));

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "AkAudio",
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Slate",
                "SlateCore",
            });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("__WINDOWS_MM__=1");
            PublicSystemLibraries.Add("winmm.lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicDefinitions.Add("__MACOSX_CORE__=1");
            PublicFrameworks.AddRange(new string[]
            {
                "CoreMIDI", "CoreAudio", "CoreFoundation"
            });
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("__LINUX_ALSA__=1");
            PublicSystemLibraries.AddRange(new string[]
            {
                "asound", "pthread"
            });
        }
    }
}

#endregion
