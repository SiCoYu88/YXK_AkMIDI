// Copyright 1998-2012 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudiokineticTools : ModuleRules
{
    public AudiokineticTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.Add("AudiokineticTools/Private");

        PrivateIncludePathModuleNames.AddRange(
            new string[]
            {
                "TargetPlatform",
                "MainFrame",
                "MovieSceneTools",
                "LevelEditor"
            });

        PublicIncludePathModuleNames.AddRange(
            new string[]
            {
                "AssetTools",
                "ContentBrowser"
            });

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "AkAudio",
                "Core",
                "InputCore",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "Slate",
                "SlateCore",
                "Json",
                "XmlParser",
                "WorkspaceMenuStructure",
                "DirectoryWatcher",
                "Projects",
                "Sequencer",
                "PropertyEditor"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "MovieScene",
                "DesktopPlatform",
                "MovieSceneTools",
                "MovieSceneTracks",
                "RenderCore",
                "AkMIDI"
            });

        if (Target.Version.MajorVersion < 5)
        {
            PublicIncludePathModuleNames.Add("Matinee");
            PublicDependencyModuleNames.Add("Matinee");
            PrivateDependencyModuleNames.Add("MatineeToLevelSequence");
            PublicDependencyModuleNames.Add("EditorStyle");
        }
    }
}
