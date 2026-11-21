// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExtendedToolkitEditor : ModuleRules
{
	public PCGExtendedToolkitEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExtendedToolkitEd";
		
		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
				"PlacementMode",
				"RenderCore",
				"RHI",
				"PCG",
				"PCGEditor",
				"PCGExtendedToolkit",
				
				// Core
				"PCGExCore",
				"PCGExCoreEditor",
				
				// Foundations
				"PCGExFoundations",
				"PCGExFoundationsEditor"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Slate",
				"SlateCore",
				"ApplicationCore",
				"AssetDefinition",
				"AssetTools",
				"AssetRegistry",
				"DeveloperSettings",
				"EditorFramework",
				"EditorSubsystem",
				"InputCore",
				"PropertyEditor",
				"TypedElementFramework",
				"UnrealEd",
			}
		);
		
	}
}