using UnrealBuildTool;

public class UnrealSessionRecorder : ModuleRules
{
	public UnrealSessionRecorder(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"ImageCore",
			"Projects",
			"RHI",
			"RenderCore"
		});
	}
}
