using UnrealBuildTool;

public class WwisePixelStreaming2 : ModuleRules
{
	public WwisePixelStreaming2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Default;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"PixelStreaming2Core"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AkAudio",
			"CoreUObject",
			"Engine",
			"PixelStreaming2",
			"Projects"
		});
	}
}
