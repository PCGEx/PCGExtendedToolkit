// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Originally ported from cavalier_contours by jbuckmccready (https://github.com/jbuckmccready/cavalier_contours)

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExElementsClipper2 : ModuleRules
{
	public PCGExElementsClipper2(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExClipper2";
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine", 
				"PCG",
				"PCGExCore",
				"PCGExBlending",
				"PCGExFilters",
				"PCGExGraphs",
				"PCGExMatching",
				"PCGExFoundations",
				"PCGExCollections", // FinalizeSpawnedActor / managed-actor spawn helpers (Clipper2 : Volume)
				"PCGExElementsClusters", // UPCGExClusterNodesData / cluster labels (Clipper2 : Straight Skeleton)
				"Chaos" // Clipper 2 Volume
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);

		if (Target.bBuildEditor)
		{
			// Editor-only: build a box render mesh on the persisted collision static mesh (Clipper2 : Volume,
			// Primitive mode) so it cooks without a "Bad MeshDescription" error.
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"MeshDescription",
					"StaticMeshDescription"
				}
			);
		}
	}
}
