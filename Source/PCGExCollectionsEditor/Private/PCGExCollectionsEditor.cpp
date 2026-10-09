// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditor.h"

#include "AssemblyRoot/PCGExAssemblyRootEditorActions.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "FileHelpers.h"
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
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/PCGExCollectionCategoryGroups.h"
#include "Details/Collections/PCGExFittingVariationsCustomization.h"
#include "Details/Collections/PCGExLevelCollectionActions.h"
#include "Details/Collections/PCGExMaterialPicksCustomization.h"
#include "Details/Collections/PCGExMeshCollectionActions.h"
#include "Details/Collections/PCGExPCGDataAssetCollectionActions.h"
#include "Details/Collections/PCGExSelectorClosestMatchAxisCustomization.h"
#include "Details/Collections/PCGExSelectorRangeAxisCustomization.h"
#include "Details/Properties/PCGExCollectionEntryPickerWidget.h"
#include "Details/Properties/PCGExRangePropertyWidget.h"
#include "Helpers/PCGExExternalPackageProducer.h"
#include "PCGExInlineWidgetRegistry.h"
#include "Details/PCGExPropertyCompiledCustomization.h"
#include "Properties/PCGExProperty_CollectionEntry.h"
#include "Properties/PCGExProperty_Range.h"
#include "Misc/CoreDelegates.h"
#include "PCGExLog.h"
#include "PCGExPropertySchemaAsset.h"
#include "UObject/UObjectIterator.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Thumbnails/PCGExCollectionThumbnailRenderer.h"
#include "Thumbnails/PCGExPCGDataAssetThumbnailRenderer.h"
#include "PCGDataAsset.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "FPCGExCollectionsEditorModule"

#undef LOCTEXT_NAMESPACE

namespace PCGExCollectionsEditorRestage
{
	/** Wall-clock half of the burst's quiet window; the frame half is in FlushPendingSourceRestages. */
	constexpr double QuietSeconds = 0.5;

	/** Longest a queued save waits for quiet, so a steady stream of saves can't starve the restage. */
	constexpr double MaxWaitSeconds = 5.0;
}

void FPCGExCollectionsEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Flush queued PCGEX_REGISTER_COLLECTION_EDITOR_TYPE registrations and any
	// per-type Customize() callbacks from each PCGEx*CollectionActions.cpp.
	FCollectionEditorTypeRegistry::ProcessPendingRegistrations();

	// Footer filter groups: display info for the PCGExCategoryGroup ids the entry structs carry.
	PCGExCollectionCategoryGroups::RegisterBuiltInGroups();

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

	// Schema-authoring rows route concrete property types through the compiled customization, which is
	// registered PER TYPE NAME -- foreign-module types must self-register or the schema UI falls back
	// to raw struct fields.
	PCGEX_REGISTER_CUSTO("PCGExProperty_CollectionEntry", FPCGExPropertyCompiledCustomization)
	PCGEX_REGISTER_CUSTO("PCGExProperty_Range", FPCGExPropertyCompiledCustomization)

	// Inline value editor for the Collection Entry property type. Edit mode = schema authoring
	// (collection box + lock + default pick); Compact mode = override rows (entry pick; collection
	// box only while unlocked).
	FPCGExInlineWidgetRegistry::Register(
		FPCGExProperty_CollectionEntry::StaticStruct()->GetFName(), EPCGExInlineWidgetMode::Edit,
		[](const TSharedRef<IPropertyHandle>& ValueHandle) { return PCGExCollectionEntryPickerWidget::Make(ValueHandle, true); });
	FPCGExInlineWidgetRegistry::Register(
		FPCGExProperty_CollectionEntry::StaticStruct()->GetFName(), EPCGExInlineWidgetMode::Compact,
		[](const TSharedRef<IPropertyHandle>& ValueHandle) { return PCGExCollectionEntryPickerWidget::Make(ValueHandle, false); });

	// Inline value editor for the Range property type. Same slider in both modes: the bounds are edited on
	// their own schema rows, never by the value widget.
	FPCGExInlineWidgetRegistry::RegisterAllModes(FPCGExProperty_Range::StaticStruct()->GetFName(), &PCGExRangePropertyWidget::Make);

	// Mosaic thumbnail renderer for all collection types. GEngine != null means engine init is
	// done and UThumbnailManager is safe to touch; otherwise defer to PostEngineInit.
	if (GEngine)
	{
		RegisterThumbnailRenderer();
	}
	else
	{
		OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPCGExCollectionsEditorModule::RegisterThumbnailRenderer);
	}

	// Covers what the other two triggers miss: source changed while the editor was closed.
	// Subscribed here, not in OnFilesLoaded: unlike OnAssetUpdatedOnDisk this doesn't fire
	// spuriously during the initial scan, and its stale check waits for the registry to stop gathering.
	OnAssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FPCGExCollectionsEditorModule::OnAssetLoaded);

	// Schema-asset edits must reach importing collections even when no details panel is open on
	// them -- the per-instance OnSchemaAssetChanged relay lives in a customization and dies with it.
	OnAnySchemaAssetChangedHandle = UPCGExPropertySchemaAsset::OnAnySchemaAssetChanged.AddRaw(this, &FPCGExCollectionsEditorModule::OnAnySchemaAssetChanged);

	// Coordinated external-package save: when a saved package hosts an
	// IPCGExExternalPackageProducer, its dirty generated packages save alongside it.
	OnPackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FPCGExCollectionsEditorModule::OnPackageSaved);

	AssemblyRootHost = MakeUnique<FPCGExAssemblyRootEditorHost>();
	AssemblyRootHost->Startup();

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
	if (AssemblyRootHost)
	{
		AssemblyRootHost->Shutdown();
		AssemblyRootHost.Reset();
	}

	FPCGExInlineWidgetRegistry::UnregisterAllModes(FPCGExProperty_CollectionEntry::StaticStruct()->GetFName());
	FPCGExInlineWidgetRegistry::UnregisterAllModes(FPCGExProperty_Range::StaticStruct()->GetFName());

	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
		AssetRegistry.OnFilesLoaded().Remove(OnFilesLoadedHandle);
		AssetRegistry.OnAssetUpdatedOnDisk().Remove(OnAssetUpdatedOnDiskHandle);
	}
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(OnObjectsReinstancedHandle);
	FCoreUObjectDelegates::OnAssetLoaded.Remove(OnAssetLoadedHandle);
	UPCGExPropertySchemaAsset::OnAnySchemaAssetChanged.Remove(OnAnySchemaAssetChangedHandle);
	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);
	UPackage::PackageSavedWithContextEvent.Remove(OnPackageSavedHandle);

	if (bThumbnailRendererRegistered && UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPCGExAssetCollection::StaticClass());
	}

	IPCGExEditorModuleInterface::ShutdownModule();
}

void FPCGExCollectionsEditorModule::RegisterThumbnailRenderer()
{
	UThumbnailManager::Get().RegisterCustomRenderer(UPCGExAssetCollection::StaticClass(), UPCGExCollectionThumbnailRenderer::StaticClass());
	// Data assets draw their mesh points -- level and assembly exports show their geometry in the
	// collection grid and the content browser instead of the class icon.
	UThumbnailManager::Get().RegisterCustomRenderer(UPCGDataAsset::StaticClass(), UPCGExPCGDataAssetThumbnailRenderer::StaticClass());
	bThumbnailRendererRegistered = true;
}

void FPCGExCollectionsEditorModule::OnAssetUpdatedOnDisk(const FAssetData& AssetData)
{
	QueueSourceRestage(AssetData, /*bReinstanced=*/ false);
}

void FPCGExCollectionsEditorModule::QueueSourceRestage(const FAssetData& AssetData, bool bReinstanced)
{
	// Never during a cook: it must stay read-only w.r.t. source content.
	if (!GEditor || !GEditor->IsTimerManagerValid() || IsRunningCookCommandlet())
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

	// An external (OFPA) actor saves into its own package, which no collection references: the
	// registry stores its outer under the LEVEL ("/Game/Maps/M.M:PersistentLevel"), so the actor's
	// object path -- and therefore its long package name -- is the level's. Walk that instead.
	// Source paths on entries are level-rooted too, so the package-name match below still holds.
	const FName ReferencedPackage = AssetData.GetOptionalOuterPathName().IsNone()
		? AssetData.PackageName
		: AssetData.GetSoftObjectPath().GetLongPackageFName();

	// Find packages that reference this asset (no load).
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(ReferencedPackage, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
	if (Referencers.IsEmpty())
	{
		return;
	}

	const UClass* CollectionClass = UPCGExAssetCollection::StaticClass();
	bool bQueued = false;

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

			FPendingSourceRestage& Pending = PendingSourceRestages.FindOrAdd(Collection);
			(bReinstanced ? Pending.Reinstanced : Pending.Saved).Add(ReferencedPackage);
			bQueued = true;
		}
	}

	if (!bQueued)
	{
		return;
	}

	// Nothing ticks editor timers in a commandlet, so a queue would never flush.
	if (IsRunningCommandlet())
	{
		RestagePendingSources();
		return;
	}

	if (!bReinstanced)
	{
		// Queue, don't restage: one save reaches here many times over several ticks -- SaveWorld rescans the map
		// synchronously, then the directory watcher reports every package the save wrote, one per OFPA actor.
		LastSourceRestageQueueTime = FPlatformTime::Seconds();
		LastSourceRestageQueueFrame = GFrameCounter;
		if (!bSavedSourcePending)
		{
			bSavedSourcePending = true;
			FirstSourceRestageQueueTime = LastSourceRestageQueueTime;
		}
	}

	if (!bSourceRestageFlushScheduled)
	{
		bSourceRestageFlushScheduled = true;
		GEditor->GetTimerManager()->SetTimerForNextTick(
			[this]()
			{
				FlushPendingSourceRestages();
			});
	}
}

void FPCGExCollectionsEditorModule::FlushPendingSourceRestages()
{
	if (!GEditor || !GEditor->IsTimerManagerValid())
	{
		bSourceRestageFlushScheduled = false;
		return;
	}

	// Only saves wait: a recompile has no directory-watcher tail, so it restages on this tick.
	if (bSavedSourcePending)
	{
		// Quiet = a whole tick since the last event AND a short wall-clock window (a large commit can spill over
		// more than one watcher poll). Not a plain SetTimer: UEditorEngine::Tick runs timers BEFORE the directory
		// watcher, and a long blocking save hands the next tick a delta big enough to fire it ahead of the events.
		const double Now = FPlatformTime::Seconds();
		const bool bQuietFrame = GFrameCounter > LastSourceRestageQueueFrame + 1;
		const bool bQuietTime = Now - LastSourceRestageQueueTime >= PCGExCollectionsEditorRestage::QuietSeconds;
		const bool bWaitedLongEnough = Now - FirstSourceRestageQueueTime >= PCGExCollectionsEditorRestage::MaxWaitSeconds;
		if (!(bQuietFrame && bQuietTime) && !bWaitedLongEnough)
		{
			GEditor->GetTimerManager()->SetTimerForNextTick(
				[this]()
				{
					FlushPendingSourceRestages();
				});
			return;
		}
	}

	bSourceRestageFlushScheduled = false;
	RestagePendingSources();
}

void FPCGExCollectionsEditorModule::RestagePendingSources()
{
	// Taken before restaging: an event raised meanwhile opens the next burst instead of this one.
	bSavedSourcePending = false;
	TMap<TWeakObjectPtr<UPCGExAssetCollection>, FPendingSourceRestage> Pending = MoveTemp(PendingSourceRestages);
	PendingSourceRestages.Reset();

	// The preference may have been turned off while the burst settled.
	if (!GetDefault<UPCGExCollectionsEditorSettings>()->bAutoRebuildOnStale)
	{
		return;
	}

	for (const TPair<TWeakObjectPtr<UPCGExAssetCollection>, FPendingSourceRestage>& PendingPair : Pending)
	{
		UPCGExAssetCollection* Collection = PendingPair.Key.Get();
		if (!Collection)
		{
			continue;
		}

		const FPendingSourceRestage& Sources = PendingPair.Value;

		// Per collection: the restages below move the registry, and a cache must never see that.
		UPCGExAssetCollection::FSourceFingerprintCache FingerprintCache;

		// One session per collection. Entries match on their advertised source packages, not Staging.Path (an
		// export-backed entry stages into the collection's own package); package names also cover BP "_C" paths.
		Collection->EDITOR_RebuildEntriesStaging([&Sources, &FingerprintCache](const FPCGExAssetCollectionEntry* InEntry)
		{
			if (InEntry->bIsSubCollection)
			{
				return false;
			}

			TSet<FSoftObjectPath> SourcePaths;
			InEntry->EDITOR_GetSourceAssetPaths(SourcePaths);

			bool bSaved = false;
			for (const FSoftObjectPath& SourcePath : SourcePaths)
			{
				const FName SourcePackage = SourcePath.GetLongPackageFName();
				if (Sources.Reinstanced.Contains(SourcePackage))
				{
					return true;
				}
				bSaved = bSaved || Sources.Saved.Contains(SourcePackage);
			}

			if (!bSaved)
			{
				return false;
			}

			// A digest still on its baseline means this content is already staged (straggler event, or another
			// tool restaged first). Unknown (0) re-stages.
			const uint64 Current = UPCGExAssetCollection::EDITOR_ComputeEntrySourceFingerprint(InEntry, &FingerprintCache);
			return Current == 0 || Current != InEntry->StagingSourceFingerprint;
		});
	}
}

void FPCGExCollectionsEditorModule::OnAssetLoaded(UObject* InObject)
{
	UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(InObject);
	if (!Collection)
	{
		return;
	}

	// Never during a cook -- WITH_EDITOR is still 1 there, so this guard is load-bearing: a cook
	// must stay read-only w.r.t. source content, and the rebuild dirties the collection package.
	if (!GEditor || !GEditor->IsTimerManagerValid() || IsRunningCookCommandlet())
	{
		return;
	}

	// Defer a tick: the rebuild cascades into UpdateStaging -> SpawnActor, unsafe inside EndLoad
	// (re-entrant load chain, GWorld mid-transition). A graph that triggered this soft-load sees
	// pre-rebuild state for its current run; later runs see fresh data.
	TWeakObjectPtr<UPCGExAssetCollection> WeakCollection(Collection);
	GEditor->GetTimerManager()->SetTimerForNextTick(
		[this, WeakCollection]()
		{
			if (UPCGExAssetCollection* Loaded = WeakCollection.Get())
			{
				PendingStaleChecks.Add(WeakCollection);
				FlushPendingStaleChecks();

				// Not gated on the staleness preference: entry references (Collection Entry
				// properties, variants) need ids to bind to, so never-rebuilt collections heal here.
				PCGExCollectionEditorUtils::EnsureEntryIds(Loaded, /*bNotify=*/false);
			}
		});
}

void FPCGExCollectionsEditorModule::FlushPendingStaleChecks()
{
	if (!GEditor || !GEditor->IsTimerManagerValid())
	{
		return;
	}

	if (!GetDefault<UPCGExCollectionsEditorSettings>()->bRebuildStaleEntriesOnOpen)
	{
		PendingStaleChecks.Reset();
		return;
	}

	// A gathering registry answers "cannot determine" for what it hasn't seen yet: ask once it is done.
	if (IAssetRegistry::GetChecked().IsGathering())
	{
		if (!bStaleCheckFlushScheduled)
		{
			bStaleCheckFlushScheduled = true;
			GEditor->GetTimerManager()->SetTimerForNextTick(
				[this]()
				{
					bStaleCheckFlushScheduled = false;
					FlushPendingStaleChecks();
				});
		}
		return;
	}

	const TArray<TWeakObjectPtr<UPCGExAssetCollection>> Pending = MoveTemp(PendingStaleChecks);
	PendingStaleChecks.Reset();

	for (const TWeakObjectPtr<UPCGExAssetCollection>& WeakCollection : Pending)
	{
		if (UPCGExAssetCollection* Loaded = WeakCollection.Get())
		{
			Loaded->EDITOR_RebuildStaleEntries();
		}
	}
}

void FPCGExCollectionsEditorModule::OnAnySchemaAssetChanged(UPCGExPropertySchemaAsset* Asset)
{
	// Cook guard mirrors OnAssetLoaded: WITH_EDITOR is still 1 there and this dirties packages.
	if (!GEditor || IsRunningCookCommandlet() || !Asset)
	{
		return;
	}

	FProperty* MemberProp = UPCGExAssetCollection::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties));

	for (TObjectIterator<UPCGExAssetCollection> It; It; ++It)
	{
		UPCGExAssetCollection* Coll = *It;
		if (!IsValid(Coll) || Coll->IsTemplate())
		{
			continue;
		}
		if (!Coll->CollectionProperties.ImportsAssetTransitive(Asset))
		{
			continue;
		}

		// Synthetic classified event: routes through the standard structural path (registry rebuild,
		// entry/category SyncToSchema, staging, OnObjectPropertyChanged -> view refresh). Deliberately
		// NOT reconciling collection-level ImportOverrides here: in-place reshape under live aliased
		// rows is unsafe -- the schema customization owns that via its parked-buffer path.
		FPropertyChangedEvent ChangedEvent(MemberProp, EPropertyChangeType::ValueSet);
		Coll->PostEditChangeProperty(ChangedEvent);
	}
}

void FPCGExCollectionsEditorModule::OnPackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext Context)
{
	if (!GEditor || !GEditor->IsTimerManagerValid() || !Package)
	{
		return;
	}

	// User-initiated editor saves only. Procedural saves (cook, autosave, resave commandlets)
	// must never fan out writes to source content -- same prohibition as
	// UPCGExPCGDataTypeState::OnHostPreSave documents for cook-time SavePackage.
	if (Context.IsProceduralSave() || IsRunningCookCommandlet() || bIsSavingExternalPackages)
	{
		return;
	}

	// Top-level assets only: producers are assets, and nested subobjects reach the same
	// implementations through their outer anyway.
	TSet<UPackage*> ExternalPackages;
	ForEachObjectWithPackage(Package, [&ExternalPackages](UObject* Object)
	{
		if (const IPCGExExternalPackageProducer* Producer = Cast<IPCGExExternalPackageProducer>(Object))
		{
			Producer->EDITOR_GetExternalPackages(ExternalPackages);
		}
		return true;
	}, /*bIncludeNestedObjects=*/ false);

	if (ExternalPackages.IsEmpty())
	{
		return;
	}

	for (UPackage* ExternalPackage : ExternalPackages)
	{
		PendingExternalPackageSaves.Add(ExternalPackage);
	}

	// One deferred flush per burst: saving inside the save callback is illegal
	// (GIsSavingPackage), and a Save-All fires this event once per package.
	if (!bExternalSaveFlushScheduled)
	{
		bExternalSaveFlushScheduled = true;
		GEditor->GetTimerManager()->SetTimerForNextTick(
			[this]()
			{
				FlushPendingExternalPackageSaves();
			});
	}
}

void FPCGExCollectionsEditorModule::FlushPendingExternalPackageSaves()
{
	bExternalSaveFlushScheduled = false;

	TArray<UPackage*> PackagesToSave;
	for (const TWeakObjectPtr<UPackage>& WeakPackage : PendingExternalPackageSaves)
	{
		UPackage* Package = WeakPackage.Get();
		if (Package && Package->IsDirty())
		{
			PackagesToSave.Add(Package);
		}
	}
	PendingExternalPackageSaves.Reset();

	if (PackagesToSave.IsEmpty())
	{
		return;
	}

	// Silent, checkout-aware save -- same policy as OFPA external actors saving with their map.
	// A Save-All that already wrote these packages leaves them clean, so this no-ops.
	TGuardValue<bool> ReentryGuard(bIsSavingExternalPackages, true);
	TArray<UPackage*> FailedPackages;
	const FEditorFileUtils::EPromptReturnCode Result =
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty=*/ true, /*bPromptToSave=*/ false, &FailedPackages);

	if (Result != FEditorFileUtils::PR_Success)
	{
		for (const UPackage* Failed : FailedPackages)
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("Generated external package '%s' could not be saved alongside its producing asset -- it stays dirty; save it manually (its on-disk content is out of sync until then)."),
			       Failed ? *Failed->GetName() : TEXT("<null>"));
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
					QueueSourceRestage(AssetData, /*bReinstanced=*/ true);
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

	// Right-click on a stock assembly root, or on its content: the same quick actions as the viewport bar.
	if (UToolMenu* ActorContextMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu"))
	{
		ActorContextMenu->AddDynamicSection(
			"PCGExAssemblyRoot", FNewToolMenuDelegate::CreateLambda(
				[](UToolMenu* ToolMenu)
				{
					if (!GEditor || GEditor->GetPIEWorldContext() || !ToolMenu)
					{
						return;
					}
					PCGExAssemblyRootEditor::ExtendActorContextMenu(ToolMenu);
				}), FToolMenuInsert(NAME_None, EToolMenuInsertType::Default));
	}
}

PCGEX_IMPLEMENT_MODULE(FPCGExCollectionsEditorModule, PCGExCollectionsEditor)
