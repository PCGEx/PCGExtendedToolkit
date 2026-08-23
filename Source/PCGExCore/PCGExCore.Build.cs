// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;                                                                                                     

public class PCGExCore : ModuleRules
{
	public PCGExCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// Set this to 0 once migration is complete
		PublicDefinitions.Add("PCGEX_SUBMODULE_CORE_REDIRECT_ENABLED=1");
		
		bool bNoPCH = Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		if (bNoPCH)                                                                    
		{                                                                                                                     
			PCHUsage = PCHUsageMode.NoPCHs;                                                                                   
		}                                                                                                                     
		else                                                                                                                  
		{                                                                                                                     
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PrivatePCHHeaderFile = "Public/PCGExCorePCH.h";
			SharedPCHHeaderFile = "Public/PCGExCorePCH.h";
		}

		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		
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
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PhysicsCore",
				"GeometryCore",
				"GeometryFramework",
				"GeometryAlgorithms"
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
					"Projects",
					"Settings",
					"Slate",
					"SlateCore",
					"ToolMenus"
				});
		}
	}
}
