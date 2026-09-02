// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExElementsActions : ModuleRules
{
	public PCGExElementsActions(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExActions";
		IWYUSupport = IWYUSupport.Full;

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
				"PCGExFilters",
				"PCGExFoundations",
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
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
					"UnrealEd",
					"Slate",
					"SlateCore",
				});
		}
	}
}
