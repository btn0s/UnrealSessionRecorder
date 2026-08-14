using UnrealBuildTool;

public class SessionTelemetry : ModuleRules
{
	public SessionTelemetry(ReadOnlyTargetRules Target) : base(Target)
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
