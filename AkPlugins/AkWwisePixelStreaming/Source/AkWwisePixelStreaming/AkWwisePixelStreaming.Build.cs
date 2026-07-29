using UnrealBuildTool;

public class AkWwisePixelStreaming : ModuleRules
{
	public AkWwisePixelStreaming(ReadOnlyTargetRules Target) : base(Target)
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
