// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditor.h"

#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "PCGExAssetTypesMacros.h"
#include "PCGExCollectionsEditorMenuUtils.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyEditorModule.h"
#include "TimerManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExActorCollectionActions.h"
#include "Details/Collections/PCGExAssetEntryCustomization.h"
#include "Details/Collections/PCGExAssetGrammarCustomization.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExFittingVariationsCustomization.h"
#include "Details/Collections/PCGExLevelCollectionActions.h"
#include "Details/Collections/PCGExMaterialPicksCustomization.h"
#include "Details/Collections/PCGExMeshCollectionActions.h"
#include "Details/Collections/PCGExPCGDataAssetCollectionActions.h"
#include "Details/Collections/PCGExSelectorClosestMatchAxisCustomization.h"
#include "Details/Collections/PCGExSelectorRangeAxisCustomization.h"
#include "Details/Collections/PCGExSkinnedMeshCollectionActions.h"
#include "Misc/CoreDelegates.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Thumbnails/PCGExCollectionThumbnailRenderer.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FPCGExCollectionsEditorModule"

#undef LOCTEXT_NAMESPACE

void FPCGExCollectionsEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Flush queued PCGEX_REGISTER_COLLECTION_EDITOR_TYPE registrations and any
	// per-type Customize() callbacks from each PCGEx*CollectionActions.cpp.
	FCollectionEditorTypeRegistry::ProcessPendingRegistrations();

	PCGEX_REGISTER_CUSTO_START

	PCGEX_REGISTER_CUSTO("PCGExFittingVariations", FPCGExFittingVariationsCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideEntry", FPCGExMaterialOverrideEntryCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideSingleEntry", FPCGExMaterialOverrideSingleEntryCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideCollection", FPCGExMaterialOverrideCollectionCustomization)
	PCGEX_REGISTER_CUSTO("PCGExAssetGrammarDetails", FPCGExAssetGrammarCustomization)
	PCGEX_REGISTER_CUSTO("PCGExSelectorRangeAxis", FPCGExSelectorRangeAxisCustomization)
	PCGEX_REGISTER_CUSTO("PCGExSelectorClosestMatchAxis", FPCGExSelectorClosestMatchAxisCustomization)

#define PCGEX_REGISTER_ENTRY_CUSTOMIZATION(_CLASS, _NAME)\
	PCGEX_REGISTER_CUSTO("PCGEx"#_CLASS"CollectionEntry", FPCGEx##_CLASS##EntryCustomization)

	PCGEX_FOREACH_ENTRY_TYPE_ALL(PCGEX_REGISTER_ENTRY_CUSTOMIZATION)

#undef PCGEX_REGISTER_ENTRY_CUSTOMIZATION

	// Mosaic thumbnail renderer for all collection types. GEngine != null means engine init is
	// done and UThumbnailManager is safe to touch; otherwise defer to PostEngineInit.
	if (GEngine)
	{
		RegisterThumbnailRenderer();
	}
	else
	{
		OnPostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FPCGExCollectionsEditorModule::RegisterThumbnailRenderer);
	}

	// Defer subscription until the AssetRegistry's initial scan completes -- it fires
	// OnAssetUpdatedOnDisk for every asset it discovers at startup, when referenced data
	// isn't yet ready. Acting then would clobber saved staging.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		OnFilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddRaw(this, &FPCGExCollectionsEditorModule::OnFilesLoaded);
	}
	else
	{
		OnFilesLoaded();
	}
}

void FPCGExCollectionsEditorModule::OnFilesLoaded()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	OnAssetUpdatedOnDiskHandle = AssetRegistry.OnAssetUpdatedOnDisk().AddRaw(this, &FPCGExCollectionsEditorModule::OnAssetUpdatedOnDisk);
	// BP edits/compiles don't fire OnAssetUpdatedOnDisk -- catch them via reinstancing.
	OnObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this, &FPCGExCollectionsEditorModule::OnObjectsReinstanced);
}

void FPCGExCollectionsEditorModule::ShutdownModule()
{
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
		AssetRegistry.OnFilesLoaded().Remove(OnFilesLoadedHandle);
		AssetRegistry.OnAssetUpdatedOnDisk().Remove(OnAssetUpdatedOnDiskHandle);
	}
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(OnObjectsReinstancedHandle);
	FCoreDelegates::GetOnPostEngineInit().Remove(OnPostEngineInitHandle);

	if (bThumbnailRendererRegistered && UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPCGExAssetCollection::StaticClass());
	}

	IPCGExEditorModuleInterface::ShutdownModule();
}

void FPCGExCollectionsEditorModule::RegisterThumbnailRenderer()
{
	UThumbnailManager::Get().RegisterCustomRenderer(UPCGExAssetCollection::StaticClass(), UPCGExCollectionThumbnailRenderer::StaticClass());
	bThumbnailRendererRegistered = true;
}

void FPCGExCollectionsEditorModule::OnAssetUpdatedOnDisk(const FAssetData& AssetData)
{
	if (!GEditor)
	{
		return;
	}
	if (!GetDefault<UPCGExCollectionsEditorSettings>()->bAutoRebuildOnStale)
	{
		return;
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Defense in depth -- shouldn't happen given the deferred subscription, but harmless.
	if (AssetRegistry.IsLoadingAssets())
	{
		return;
	}

	// Find packages that reference this asset (no load).
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(AssetData.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
	if (Referencers.IsEmpty())
	{
		return;
	}

	const UClass* CollectionClass = UPCGExAssetCollection::StaticClass();

	for (const FName& ReferencerPackage : Referencers)
	{
		// Class metadata only -- no load.
		TArray<FAssetData> ReferencerAssets;
		AssetRegistry.GetAssetsByPackageName(ReferencerPackage, ReferencerAssets, /*bIncludeOnlyOnDiskAssets=*/ true);

		for (const FAssetData& ReferencerAsset : ReferencerAssets)
		{
			const UClass* AssetClass = ReferencerAsset.GetClass();
			if (!AssetClass || !AssetClass->IsChildOf(CollectionClass))
			{
				continue;
			}

			// Only act on collections that are already loaded. Unloaded ones are not touched
			// here -- they'll be considered when the user next opens them via manual rebuild.
			UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(ReferencerAsset.GetSoftObjectPath().ResolveObject());
			if (!Collection)
			{
				continue;
			}

			// Per-entry rebuild: match against the entry's advertised source paths.
			// EDITOR_GetSourceAssetPaths() returns the *external* refs that should trigger
			// a rebuild when updated on disk -- which for some entry types (e.g. PCGDataAsset
			// entries in Level mode) is NOT Staging.Path. Matching by package name also
			// handles BP class paths where the path ends in "_C".
			Collection->ForEachEntry([Collection, &AssetData](const FPCGExAssetCollectionEntry* InEntry, int32 i)
			{
				if (InEntry->bIsSubCollection)
				{
					return;
				}

				TSet<FSoftObjectPath> SourcePaths;
				InEntry->EDITOR_GetSourceAssetPaths(SourcePaths);

				for (const FSoftObjectPath& SourcePath : SourcePaths)
				{
					if (SourcePath.GetLongPackageFName() == AssetData.PackageName)
					{
						Collection->EDITOR_RebuildEntryStaging(i);
						return;
					}
				}
			});
		}
	}
}

void FPCGExCollectionsEditorModule::OnObjectsReinstanced(const TMap<UObject*, UObject*>& OldToNewMap)
{
	if (!GEditor)
	{
		return;
	}
	// Failsafe for early startup
	if (!GEditor->IsTimerManagerValid())
	{
		return;
	}
	if (!GetDefault<UPCGExCollectionsEditorSettings>()->bAutoRebuildOnStale)
	{
		return;
	}

	// Reinstancing fires DURING the BP recompile flow -- the new class exists but isn't
	// fully settled (CDO, components, etc. may still be finalising). Spawning a temp actor
	// at this point gives unstable bounds. Capture the affected packages and defer the
	// actual rebuild to the next tick when reinstancing is complete.
	TSet<FName> ChangedPackages;
	for (const TPair<UObject*, UObject*>& Pair : OldToNewMap)
	{
		UObject* NewObj = Pair.Value;
		if (!NewObj)
		{
			continue;
		}
		UPackage* Package = NewObj->GetOutermost();
		if (!Package || Package == GetTransientPackage())
		{
			continue;
		}
		ChangedPackages.Add(Package->GetFName());
	}

	if (ChangedPackages.IsEmpty())
	{
		return;
	}

	GEditor->GetTimerManager()->SetTimerForNextTick(
		[this, ChangedPackages]()
		{
			if (!GEditor)
			{
				return;
			}
			const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			for (const FName& PackageName : ChangedPackages)
			{
				TArray<FAssetData> Assets;
				AssetRegistry.GetAssetsByPackageName(PackageName, Assets, /*bIncludeOnlyOnDiskAssets=*/ false);
				for (const FAssetData& AssetData : Assets)
				{
					OnAssetUpdatedOnDisk(AssetData);
				}
			}
		});
}

void FPCGExCollectionsEditorModule::RegisterMenuExtensions()
{
	IPCGExEditorModuleInterface::RegisterMenuExtensions();

	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* WorldAssetMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AssetActionsSubMenu"))
	{
		// Use a dynamic section here because we might have plugins registering at a later time
		FToolMenuSection& Section = WorldAssetMenu->AddDynamicSection(
			"PCGEx", FNewToolMenuDelegate::CreateLambda(
				[this](UToolMenu* ToolMenu)
				{
					if (!GEditor || GEditor->GetPIEWorldContext() || !ToolMenu)
					{
						return;
					}
					if (UContentBrowserAssetContextMenuContext* AssetMenuContext = ToolMenu->Context.FindContext<UContentBrowserAssetContextMenuContext>())
					{
						PCGExCollectionsEditorMenuUtils::CreateOrUpdatePCGExAssetCollectionsFromMenu(ToolMenu, AssetMenuContext->SelectedAssets);
					}
				}), FToolMenuInsert(NAME_None, EToolMenuInsertType::Default));
	}
}

PCGEX_IMPLEMENT_MODULE(FPCGExCollectionsEditorModule, PCGExCollectionsEditor)
