// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

// UncookedOnly host for collections-related K2 nodes. Kept separate from the Editor-typed
// PCGExCollectionsEditor so Blueprints containing these nodes compile on load in uncooked
// -game runs (Editor modules don't load there); mirrors why PCGExPropertiesEditor is
// UncookedOnly.
public class PCGExCollectionsUncooked : ModuleRules
{
	public PCGExCollectionsUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH"));
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExCollectionsUnc";

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
				"Engine",
				"PCG",
				"PCGExCore",
				"PCGExProperties",
				"PCGExPropertiesEditor", // For PCGExK2NodeTypeHelpers
				"PCGExCollections"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"BlueprintGraph",
				"KismetCompiler",
				"Kismet",
				"ToolMenus",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
