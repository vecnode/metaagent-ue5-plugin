using UnrealBuildTool;

public class MetaAgentPluginEditor : ModuleRules
{
	public MetaAgentPluginEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MetaAgentPlugin"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"EditorFramework",
				"Slate",
				"SlateCore",
				"PropertyEditor",
				"ToolMenus",
				"LevelEditor",
				"UnrealEd"
			}
		);
	}
}
