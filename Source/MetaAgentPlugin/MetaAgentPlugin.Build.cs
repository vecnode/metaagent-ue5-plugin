using UnrealBuildTool;
using System.IO;

public class MetaAgentPlugin : ModuleRules
{
	public MetaAgentPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory),
				Path.GetFullPath(Path.Combine(PluginDirectory, "metaagent", "src")),
				Path.GetFullPath(Path.Combine(PluginDirectory, "metaagent"))
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Niagara",
				"RenderCore",
				"RHI",
				"EngineSettings",
				"InputCore",
				"EnhancedInput",
				"AIModule",
				"NavigationSystem",
				"StateTreeModule",
				"GameplayStateTreeModule",
				"HTTP",
				"HTTPServer",
				"Json",
				"JsonUtilities",
				"UMG",
				"Slate",
				"DeveloperSettings",
				"GameplayTags",
				"ImageWrapper",
				"ImageCore",
				"MovieSceneCapture",
				"AVIWriter"
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
