// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExElementsClustersSketchEditor : ModuleRules
{
	public PCGExElementsClustersSketchEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH"));
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExClustersSketchEd";

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"PCG",
				"PCGExCore",
				"PCGExElementsClustersSketch",
				// Lattice basis: the edit controller snaps against it directly.
				"PCGExGraphs",
				"PCGExCoreEditor",
				"AssetDefinition"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"AssetTools",
				"Slate",
				"SlateCore",
				"PropertyEditor",
				"ToolMenus",
				"Projects",
				"EditorFramework",
				"EditorSubsystem",
				"AdvancedPreviewScene",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
				"WorkspaceMenuStructure"
			}
		);
	}
}
