using UnrealBuildTool;

public class UnrealSessionRecorderEditor : ModuleRules
{
	public UnrealSessionRecorderEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"UnrealSessionRecorder"
		});
	}
}
