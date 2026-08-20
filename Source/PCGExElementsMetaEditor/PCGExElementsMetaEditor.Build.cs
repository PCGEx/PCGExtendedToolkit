// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

public class PCGExElementsMetaEditor : ModuleRules
{
	public PCGExElementsMetaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH"));
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;
		ShortName = "PCGExMetaEd";

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"UnrealEd",
				"Engine",
				"PCG",
				"PCGExCore",
				"PCGExCoreEditor",
				"PCGExElementsMeta",
				"AssetDefinition"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PCG",
				"Slate",
				"SlateCore",
				"PropertyEditor"
			}
		);
	}
}
