using UnrealBuildTool;

public class WwiseMediaRuntime : ModuleRules
{
    public WwiseMediaRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "AkAudio", "WwiseSoundEngine"
        });
        PrivateDependencyModuleNames.AddRange(new[] {
            "Media", "MediaAssets", "MediaUtils"
        });
    }
}
