// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExtendedToolkit.h"
#if WITH_EDITOR
#include "Editor/PCGExSubModules.generated.h"
#else
#include "PCGExSubModules.generated.h"
#endif
#include "PCGExVersion.h"

#if WITH_EDITOR
#include "PCGExCoreSettingsCache.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerTypes.h"
#include "UObject/ICookInfo.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Helpers/PCGExCookDependencyProvider.h"
#endif

#include "PCGExGlobalSettings.h"
#include "PCGExModuleInterface.h"
#include "AssetRegistry/AssetData.h"

#define LOCTEXT_NAMESPACE "FPCGExtendedToolkitModule"

void FPCGExtendedToolkitModule::StartupModule()
{
	FModuleManager::Get().LoadModuleChecked<IModuleInterface>("PCG");

#if WITH_EDITOR

	ModifyCookDelegateHandle = UE::Cook::FDelegates::ModifyCook.AddLambda(
		[](UE::Cook::ICookInfo& CookInfo, TArray<UE::Cook::FPackageCookRule>& InOutPackageCookRules)
		{
			IAssetRegistry& Registry = UAssetManager::Get().GetAssetRegistry();
			const FName FolderInstigator(TEXT("PCGExAlwaysCookAssets"));
			const FName ProviderInstigator(TEXT("PCGExCookDependencyProvider"));

			// Thanks @jenkinsgage
			// Force-cook every asset shipped inside this plugin's content folder.
			TArray<FAssetData> PCGExAssets;
			Registry.GetAssetsByPath(TEXT("/PCGExtendedToolkit"), PCGExAssets, true);
			for (const FAssetData& Asset : PCGExAssets)
			{
				InOutPackageCookRules.Emplace(Asset.PackageName, FolderInstigator, UE::Cook::EPackageCookRule::AddToCook);
			}

			// Collect concrete UClasses implementing IPCGExCookDependencyProvider for a single batched registry query.
			TArray<FTopLevelAssetPath> ProviderClassPaths;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					continue;
				}
				if (Class->ImplementsInterface(UPCGExCookDependencyProvider::StaticClass()))
				{
					ProviderClassPaths.Add(Class->GetClassPathName());
				}
			}

			if (ProviderClassPaths.IsEmpty())
			{
				return;
			}

			FARFilter Filter;
			Filter.ClassPaths = MoveTemp(ProviderClassPaths);
			Filter.bRecursiveClasses = false; // leaves already enumerated

			TArray<FAssetData> ProviderAssets;
			Registry.GetAssets(Filter, ProviderAssets);

			for (const FAssetData& Asset : ProviderAssets)
			{
				// Soft refs into /Game won't drag the provider asset into the cook on their own.
				InOutPackageCookRules.Emplace(Asset.PackageName, ProviderInstigator, UE::Cook::EPackageCookRule::AddToCook);

				UObject* Obj;
				{
					FCookLoadScope CookLoadScope(ECookLoadType::EditorOnly);
					Obj = Asset.GetAsset();
				}
				if (!Obj) { continue; }

				IPCGExCookDependencyProvider* Provider = Cast<IPCGExCookDependencyProvider>(Obj);
				if (!Provider) { continue; }

				TSet<FSoftObjectPath> Paths;
				Provider->GetCookDependencyAssetPaths(Paths);

				for (const FSoftObjectPath& Path : Paths)
				{
					const FName PackageName = Path.GetLongPackageFName();
					if (!PackageName.IsNone())
					{
						InOutPackageCookRules.Emplace(PackageName, ProviderInstigator, UE::Cook::EPackageCookRule::AddToCook);
					}
				}
			}
		}
		);

#endif

	const TMap<FString, TArray<FString>>& Dependencies = PCGExSubModules::GetModuleDependencies();
	TSet<FString> Loaded;

	// Recursive loader
	TFunction<void(const FString&)> LoadWithDeps = [&](const FString& ModuleName)
	{
		if (Loaded.Contains(ModuleName))
		{
			return;
		}

		// Load dependencies first
		if (const TArray<FString>* Deps = Dependencies.Find(ModuleName))
		{
			for (const FString& Dep : *Deps)
			{
				LoadWithDeps(Dep);
			}
		}

		// Now load this module
		if (!FModuleManager::Get().IsModuleLoaded(*ModuleName))
		{
			FModuleManager::Get().LoadModule(*ModuleName);
		}
		Loaded.Add(ModuleName);
	};

	// Load all enabled modules (dependencies will be loaded first)
	for (const FString& ModuleName : PCGExSubModules::GetEnabledModules())
	{
		LoadWithDeps(ModuleName);
	}

	GetDefault<UPCGExGlobalSettings>()->UpdateSettingsCaches();

#pragma region Push Pins

#if WITH_EDITOR

	int32 PinIndex = -1;

#pragma region OUT

#if PCGEX_ENGINE_VERSION < 507
	// Register pins as extra icons on 5.6

	// Out pin map
	PCGEX_EMPLACE_PIN_OUT(OUT_Filter, "PCGEx Filter");
	PCGEX_MAP_PIN_OUT("Filter")

	PCGEX_EMPLACE_PIN_OUT(OUT_FilterCollection, "PCGEx Data Filter");
	PCGEX_MAP_PIN_OUT("Data Filter")

	PCGEX_EMPLACE_PIN_OUT(OUT_FilterEdge, "PCGEx Edge Filter");
	PCGEX_MAP_PIN_OUT("Edge Filter")

	PCGEX_EMPLACE_PIN_OUT(OUT_FilterVtx, "PCGEx Vtx Filter");
	PCGEX_MAP_PIN_OUT("Node Filter")

	PCGEX_EMPLACE_PIN_OUT(OUT_ClusterState, "PCGEx State Cluster");
	PCGEX_MAP_PIN_OUT("State")

	PCGEX_EMPLACE_PIN_OUT(OUT_Heuristics, "PCGEx Heuristic");
	PCGEX_MAP_PIN_OUT("Heuristics")

	PCGEX_EMPLACE_PIN_OUT(OUT_Probe, "PCGEx Probe");
	PCGEX_MAP_PIN_OUT("Probe")

	PCGEX_EMPLACE_PIN_OUT(OUT_Noise3D, "PCGEx Noise");
	PCGEX_MAP_PIN_OUT("Noise")

	PCGEX_EMPLACE_PIN_OUT(OUT_SortRule, "PCGEx Sort Rule");
	PCGEX_MAP_PIN_OUT("SortRule")
	PCGEX_MAP_PIN_OUT("SortingRule")

	PCGEX_EMPLACE_PIN_OUT(OUT_TexParam, "PCGEx Texture Params");
	PCGEX_MAP_PIN_OUT("TextureParam")

	PCGEX_EMPLACE_PIN_OUT(OUT_PartitionRule, "PCGEx Partition Rule");
	PCGEX_MAP_PIN_OUT("PartitionRule")

	PCGEX_EMPLACE_PIN_OUT(OUT_VtxProperty, "PCGEx Vtx Property");
	PCGEX_MAP_PIN_OUT("Property")

	PCGEX_EMPLACE_PIN_OUT(OUT_Action, "PCGEx Action");
	PCGEX_MAP_PIN_OUT("Action")

	PCGEX_EMPLACE_PIN_OUT(OUT_BlendOp, "PCGEx Blending");
	PCGEX_MAP_PIN_OUT("Blend Op")

	PCGEX_EMPLACE_PIN_OUT(OUT_Shape, "PCGEx Shape Builder");
	PCGEX_MAP_PIN_OUT("Shape Builder")

	PCGEX_EMPLACE_PIN_OUT(OUT_Tensor, "PCGEx Tensor");
	PCGEX_MAP_PIN_OUT("Tensor")

	PCGEX_EMPLACE_PIN_OUT(OUT_Picker, "PCGEx Picker");
	PCGEX_MAP_PIN_OUT("Picker")

	PCGEX_EMPLACE_PIN_OUT(OUT_FillControl, "PCGEx Fill Control");
	PCGEX_MAP_PIN_OUT("Fill Control")

	PCGEX_EMPLACE_PIN_OUT(OUT_MatchRule, "PCGEx Data Matching Rule");
	PCGEX_MAP_PIN_OUT("Match Rule")

	PCGEX_EMPLACE_PIN_OUT(OUT_Selector, "PCGEx Selector");
	PCGEX_MAP_PIN_OUT("Selector")

	PCGEX_EMPLACE_PIN_OUT(OUT_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_OUT("Vtx")
	PCGEX_MAP_PIN_OUT("Unmatched Vtx")

	PCGEX_EMPLACE_PIN_OUT(OUT_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_OUT("Edges")
	PCGEX_MAP_PIN_OUT("Unmatched Edges")

#else

	PCGEX_EMPLACE_PIN_OUT(OUT_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_OUT("Vtx")
	PCGEX_MAP_PIN_OUT("Unmatched Vtx")

	PCGEX_EMPLACE_PIN_OUT(OUT_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_OUT("Edges")
	PCGEX_MAP_PIN_OUT("Unmatched Edges")

#endif

#pragma endregion

#pragma region IN

#if PCGEX_ENGINE_VERSION < 507
	// Register pins as extra icons on 5.6

	// In pin map
	PCGEX_EMPLACE_PIN_IN(IN_Filter, "Expects PCGEx Filters, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Filters")
	PCGEX_MAP_PIN_IN("Point Filters")
	PCGEX_MAP_PIN_IN("Conditions Filters")
	PCGEX_MAP_PIN_IN("Keep Conditions")
	PCGEX_MAP_PIN_IN("Skip Conditions")
	PCGEX_MAP_PIN_IN("Generator Filters")
	PCGEX_MAP_PIN_IN("Connectable Filters")
	PCGEX_MAP_PIN_IN("Can Be Cut Conditions")
	PCGEX_MAP_PIN_IN("Can Cut Conditions")
	PCGEX_MAP_PIN_IN("Bevel Conditions")
	PCGEX_MAP_PIN_IN("Trigger Conditions")
	PCGEX_MAP_PIN_IN("Shift Conditions")
	PCGEX_MAP_PIN_IN("Split Conditions")
	PCGEX_MAP_PIN_IN("Toggle Conditions")
	PCGEX_MAP_PIN_IN("Start Conditions")
	PCGEX_MAP_PIN_IN("Stop Conditions")
	PCGEX_MAP_PIN_IN("Pin Conditions")
	PCGEX_MAP_PIN_IN("Conditions")
	PCGEX_MAP_PIN_IN("Flip Conditions")
	PCGEX_MAP_PIN_IN("Tracker Filters")

	// Ahem.
	for (int f = 0; f < 42; f++)
	{
		FString SI = FString::Printf(TEXT("%d"), f + 0);
		PCGEX_CORE_SETTINGS.OutPinInfosMap.Add(FName(TEXT("→ ") + SI), PinIndex);
	}

	PCGEX_EMPLACE_PIN_IN(IN_FilterEdge, "Expects PCGEx Filers or Edge Filters, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Edge Filters")
	PCGEX_MAP_PIN_IN("EdgeFilters")

	PCGEX_EMPLACE_PIN_IN(IN_Selector, "Expects PCGEx Selector, supports single input.");
	PCGEX_MAP_PIN_IN("Selector")

	PCGEX_EMPLACE_PIN_IN(IN_FilterVtx, "Expects PCGEx Filters or Vtx Filter, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Vtx Filters")
	PCGEX_MAP_PIN_IN("VtxFilters")
	PCGEX_MAP_PIN_IN("NodeFilters")
	PCGEX_MAP_PIN_IN("Break Conditions")

	PCGEX_EMPLACE_PIN_IN(IN_ClusterState, "Expects PCGEx State nodes, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("States")

	PCGEX_EMPLACE_PIN_IN(IN_Heuristics, "Expects PCGEx Heuristics, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Heuristics")

	PCGEX_EMPLACE_PIN_IN(IN_Probe, "Expects PCGEx Probes, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Probes")

	PCGEX_EMPLACE_PIN_IN(IN_SortRule, "Expects PCGEx Sort Rules, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("SortingRules")
	PCGEX_MAP_PIN_IN("SortRule")
	PCGEX_MAP_PIN_IN("SortRules")
	PCGEX_MAP_PIN_IN("Direction Sorting")

	PCGEX_EMPLACE_PIN_IN(IN_TexParam, "Expects PCGEx Texture Params, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("TextureParams")

	PCGEX_EMPLACE_PIN_IN(IN_PartitionRule, "Expects PCGEx Partition Rules, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("PartitionRules")

	PCGEX_EMPLACE_PIN_IN(IN_VtxProperty, "Expects PCGEx Vtx Properties, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Properties")

	PCGEX_EMPLACE_PIN_IN(IN_Action, "Expects PCGEx Actions, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Actions")

	PCGEX_EMPLACE_PIN_IN(IN_BlendOp, "Expects PCGEx Blending ops, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Blend Ops")

	PCGEX_EMPLACE_PIN_IN(OUT_Shape, "Expects PCGEx Shape Builders, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Shape Builders")

	PCGEX_EMPLACE_PIN_IN(IN_Tensor, "Expects PCGEx Tensors, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Tensors")
	PCGEX_MAP_PIN_IN("Parent Tensor")

	PCGEX_EMPLACE_PIN_IN(IN_Picker, "PCGEx Pickers, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Pickers")

	PCGEX_EMPLACE_PIN_IN(IN_FillControl, "PCGEx Fill Controls, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Fill Controls")

	PCGEX_EMPLACE_PIN_IN(IN_Noise3D, "PCGEx Noises, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Noises")
	PCGEX_MAP_PIN_IN("Masks")

	PCGEX_EMPLACE_PIN_IN(IN_MatchRule, "PCGEx Data Match Rules, supports multiple inputs.");
	PCGEX_MAP_PIN_IN("Match Rules")
	PCGEX_MAP_PIN_IN("Match Rules (Edges)")

	PCGEX_EMPLACE_PIN_IN(IN_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_IN("Vtx")

	PCGEX_EMPLACE_PIN_IN(IN_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_IN("Edges")

	PCGEX_EMPLACE_PIN_IN(IN_Special, "Attribute set whose values will be used to override a specific internal module.");
	PCGEX_MAP_PIN_IN("Overrides : Blending")
	PCGEX_MAP_PIN_IN("Overrides : Refinement")
	PCGEX_MAP_PIN_IN("Overrides : Graph Builder")
	PCGEX_MAP_PIN_IN("Overrides : Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Start Tangents")
	PCGEX_MAP_PIN_IN("Overrides : End Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Goal Picker")
	PCGEX_MAP_PIN_IN("Overrides : Search")
	PCGEX_MAP_PIN_IN("Overrides : Orient")
	PCGEX_MAP_PIN_IN("Overrides : Smoothing")
	PCGEX_MAP_PIN_IN("Overrides : Packer")

#else

	PCGEX_EMPLACE_PIN_IN(IN_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_IN("Vtx")

	PCGEX_EMPLACE_PIN_IN(IN_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_IN("Edges")

#endif

	PCGEX_EMPLACE_PIN_IN(IN_Special, "Attribute set whose values will be used to override a specific internal module.");
	PCGEX_MAP_PIN_IN("Overrides : Blending")
	PCGEX_MAP_PIN_IN("Overrides : Refinement")
	PCGEX_MAP_PIN_IN("Overrides : Graph Builder")
	PCGEX_MAP_PIN_IN("Overrides : Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Start Tangents")
	PCGEX_MAP_PIN_IN("Overrides : End Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Goal Picker")
	PCGEX_MAP_PIN_IN("Overrides : Search")
	PCGEX_MAP_PIN_IN("Overrides : Orient")
	PCGEX_MAP_PIN_IN("Overrides : Smoothing")
	PCGEX_MAP_PIN_IN("Overrides : Packer")

#pragma endregion

#endif

#pragma endregion
}

void FPCGExtendedToolkitModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module

#if WITH_EDITOR
	UE::Cook::FDelegates::ModifyCook.Remove(ModifyCookDelegateHandle);
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExtendedToolkitModule, PCGExtendedToolkit)
