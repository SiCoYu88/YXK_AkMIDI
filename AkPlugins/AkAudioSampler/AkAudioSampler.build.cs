using UnrealBuildTool;
using System.IO;

public class AkAudioSampler : ModuleRules
{
#if WITH_FORWARDED_MODULE_RULES_CTOR
   public AkAudioSampler(ReadOnlyTargetRules Target) : base(Target)
#else
    public AkAudioSampler(TargetInfo Target)
#endif
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"AkAudio",
				"Niagara",
				"NiagaraCore",
				"RenderCore",
				"RHI",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects",
				"VectorVM",
				"WwiseSoundEngine"
				// ... add private dependencies that you statically link with here ...	
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"NiagaraEditor",
					"UnrealEd"
				}
				);
		}
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        if (Target.Platform == UnrealTargetPlatform.Android
            && Target.Type != TargetType.Server && Target.Type != TargetType.Program)
        {
            AddAndroidLibraries(Target);
        }
    }

    private void AddAndroidLibraries(ReadOnlyTargetRules Target)
    {
        // Resolve the actual WwiseSoundEngine plugin, including engine installations.
        string ThirdParty = Path.GetFullPath(Path.Combine(
            GetModuleDirectory("WwiseSoundEngine"), "..", "..", "ThirdParty"));
        string Configuration = "Profile";
        if (Target.Configuration == UnrealTargetConfiguration.Debug
            || Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            Configuration = "Debug";
        }
        else if (Target.Configuration == UnrealTargetConfiguration.Shipping
            || Target.Configuration == UnrealTargetConfiguration.Test)
        {
            Configuration = "Release";
        }

        foreach (string Abi in new[] { "arm64-v8a", "x86_64" })
        {
            if (!Target.Architectures.Contains(Abi == "arm64-v8a" ? UnrealArch.Arm64 : UnrealArch.X64))
            {
                continue;
            }
            string Library = Path.Combine(ThirdParty, "Android", Abi, Configuration,
                "lib", "libAudioBusHackerFX.a");
            if (!File.Exists(Library))
            {
                throw new BuildException("AkAudioSampler requires the AudioBusHacker Android static library: "
                    + Library + ". Build AkSDK/AudioBusHacker/build_android.bat all with the matching "
                    + "Wwise SDK, then deploy using --wwise-plugin <WwiseSoundEngine plugin directory>.");
            }
            PublicAdditionalLibraries.Add(Library);
            ExternalDependencies.Add(Library);
        }
    }
}
