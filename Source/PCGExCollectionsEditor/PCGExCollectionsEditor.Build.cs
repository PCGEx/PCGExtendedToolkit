// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExCollectionsEditor : ModuleRules
{
	public PCGExCollectionsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExCollectionsEd";
		bUseUnity = true;

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);


		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
		);


		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"UnrealEd",
				"Settings",
				"Engine",
				"PCG",
				"PCGExCore",
				"PCGExCoreEditor",
				"PCGExCollections",
				"PCGExFoundations",
				"PCGExProperties",
				"PCGExPropertiesEditor", // For FPCGExPropertyOverrides customization
				"AssetDefinition"
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ContentBrowser",
				"InputCore",
				"RenderCore",
				"AssetTools",
				"AssetRegistry",
				"Slate",
				"SlateCore",
				"PropertyPath",
				"DeveloperSettings",
				"Slate",
				"SlateCore",
				"PropertyEditor",
				"EditorWidgets",
				"ToolMenus", "AppFramework"
			}
		);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					
				});
		}
	}
}
