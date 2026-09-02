// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExActorCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif

#include "PCGComponent.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExLog.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExSchemaMerging.h"
#include "PCGExSocketProvider.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Helpers/PCGExActorHelpers.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "UObject/UObjectThreadContext.h"

// Static-init type registration: TypeId=Actor, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Actor, UPCGExActorCollection, FPCGExActorCollectionEntry, "Actor Collection", Base)

UPCGExActorCollection::UPCGExActorCollection(const FObjectInitializer& ObjectInitializer)
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;

	UClass* EvalClass = Settings.DefaultBoundsEvaluatorClass
		? Settings.DefaultBoundsEvaluatorClass.Get()
		: UPCGExDefaultBoundsEvaluator::StaticClass();

	BoundsEvaluator = Cast<UPCGExBoundsEvaluator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("BoundsEvaluator"),
		                                         UPCGExBoundsEvaluator::StaticClass(), EvalClass, false, false));
}

#pragma region FPCGExActorCollectionEntry

bool FPCGExActorCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Actor.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

// Spawns a temporary actor in-editor to compute bounds via GetActorBounds(),
// then immediately destroys it. Only works in editor context (non-editor falls back to empty bounds).
void FPCGExActorCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Actor.ToSoftObjectPath();
	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Actor.ToSoftObjectPath());

	if (UClass* ActorClass = Actor.Get())
	{
#if WITH_EDITOR
		UWorld* World = GWorld;
		if (!World)
		{
			UE_LOG(LogPCGEx, Error, TEXT("No world to compute actor bounds!"));
			return;
		}

		// The guard, the spawn and the teardown all live in PCGExHelpers::WithTempActor -- shared
		// with every other caller that needs a real instance to inspect.
		if (!PCGExHelpers::IsSpawnSafe(World))
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("World not in a spawn-safe state (type=%d, tearing=%d, hasLevel=%d, routingPostLoad=%d); skipping bounds for '%s'."),
			       static_cast<int32>(World->WorldType.GetValue()),
			       World->bIsTearingDown ? 1 : 0,
			       World->PersistentLevel ? 1 : 0,
			       FUObjectThreadContext::Get().IsRoutingPostLoad ? 1 : 0,
			       *ActorClass->GetPathName());
			return;
		}

		const bool bSpawned = PCGExHelpers::WithTempActor(
			World, ActorClass, [&](AActor* TempActor)
			{
				// Evaluator via the type-globals seam; hosts without a block take the GetActorBounds fallback.
				FPCGExActorCollectionGlobals Globals;
				const bool bHasGlobals = OwningCollection && OwningCollection->GetTypeGlobals(Globals);
				if (bHasGlobals && Globals.BoundsEvaluator)
				{
					const FBox WorldBounds = Globals.BoundsEvaluator->EvaluateActorBounds(
						TempActor, const_cast<UPCGExAssetCollection*>(OwningCollection), InInternalIndex);
					Staging.Bounds = WorldBounds.IsValid ? WorldBounds : FBox(ForceInit);
				}
				else
				{
					FVector Origin, Extents;
					TempActor->GetActorBounds(false, Origin, Extents);
					Staging.Bounds = FBox(Origin - Extents, Origin + Extents);
				}

				// Inspect for PCG components
				TInlineComponentArray<UPCGComponent*, 1> PCGComps;
				TempActor->GetComponents(PCGComps);
				bHasPCGComponent = !PCGComps.IsEmpty();
				CachedPCGGraph = (bHasPCGComponent && PCGComps[0]->GetGraph())
					? TSoftObjectPtr<UPCGGraphInterface>(FSoftObjectPath(PCGComps[0]->GetGraph()))
					: nullptr;

				// Temp actor is at FTransform::Identity, so component world transform == relative to actor
				TArray<UPCGExSocketComponent*> SocketComps;
				TempActor->GetComponents<UPCGExSocketComponent>(SocketComps);
				for (UPCGExSocketComponent* SC : SocketComps)
				{
					FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
						SC->GetSocketName_Implementation(),
						SC->GetSocketTransform_Implementation(),
						SC->GetSocketTag_Implementation());
					NewSocket.bManaged = true;
				}
			});

		if (!bSpawned)
		{
			UE_LOG(LogPCGEx, Error, TEXT("Failed to create temp actor!"));
			return;
		}

#else
		Staging.Bounds = FBox(ForceInit);
		bHasPCGComponent = false;
		CachedPCGGraph = nullptr;
		UE_LOG(LogPCGEx, Error, TEXT("UpdateStaging called in non-editor context. This is not supported for Actor Collections."));
#endif
	}

#if WITH_EDITOR
	// Delta capture from a placed actor in a level. An unreachable actor keeps the previous delta.
	if (DeltaSourceActor.ToSoftObjectPath().IsValid())
	{
		TSharedPtr<FStreamableHandle> LevelHandle;
		FString Failure;
		if (AActor* FoundActor = PCGExCollections::ResolveLevelActor(DeltaSourceActor.ToSoftObjectPath(), LevelHandle, &Failure))
		{
			if (Actor.Get() && FoundActor->IsA(Actor.Get()))
			{
				DeltaCollateralPaths.Reset();
				SerializedPropertyDelta = PCGExActorDelta::SerializeActorDelta(FoundActor, &DeltaCollateralPaths);
			}
			else if (!Actor.ToSoftObjectPath().IsValid())
			{
				// Auto-populate Actor class from the found actor
				Actor = TSoftClassPtr<AActor>(FSoftClassPath(FoundActor->GetClass()));
				Staging.Path = Actor.ToSoftObjectPath();
				DeltaCollateralPaths.Reset();
				SerializedPropertyDelta = PCGExActorDelta::SerializeActorDelta(FoundActor, &DeltaCollateralPaths);
			}
			else
			{
				UE_LOG(LogPCGEx, Warning, TEXT("Delta source actor class mismatch -- expected '%s', found '%s'"),
				       *Actor.ToSoftObjectPath().ToString(), *FSoftClassPath(FoundActor->GetClass()).ToString());
			}
		}
		else
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Delta source actor unavailable: %s -- keeping the previous delta."), *Failure);
		}

		PCGExHelpers::SafeReleaseHandle(LevelHandle);
	}
#endif

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExActorCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Actor = TSoftClassPtr<AActor>(InPath);
}

bool FPCGExActorCollectionEntry::OnHostPostLoad(UPCGExAssetCollection* Host)
{
	const bool bHasLegacyPair = !DeltaSourceLevel_DEPRECATED.IsNull() && DeltaSourceActorName_DEPRECATED != NAME_None;
	if (!bHasLegacyPair)
	{
		return false;
	}

	// The name lookup this replaces was never more exact than the persistent-level path; an actor
	// renamed since the last save fails loudly at the next staging instead of silently matching.
	if (DeltaSourceActor.IsNull())
	{
		DeltaSourceActor = TSoftObjectPtr<AActor>(FSoftObjectPath(
			DeltaSourceLevel_DEPRECATED.ToSoftObjectPath().GetAssetPath(),
			FString::Printf(TEXT("PersistentLevel.%s"), *DeltaSourceActorName_DEPRECATED.ToString())));
	}
	DeltaSourceLevel_DEPRECATED.Reset();
	DeltaSourceActorName_DEPRECATED = NAME_None;
	return true;
}

#if WITH_EDITOR
void FPCGExActorCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	if (!bIsSubCollection)
	{
		if (!Actor.ToSoftObjectPath().IsValid())
		{
			bHasPCGComponent = false;
			CachedPCGGraph = nullptr;
		}

		// Do NOT clear SerializedPropertyDelta here. Embedded entries populated by
		// UPCGExDefaultLevelDataExporter never set DeltaSourceLevel/DeltaSourceActorName
		// (the delta is captured programmatically from live actors at export time), so
		// clearing on empty DeltaSource would wipe legitimate embedded deltas on every
		// rebuild. User-authored entries that clear their DeltaSource can manually clear
		// the delta via the details panel if needed.
	}
	else
	{
		bHasPCGComponent = false;
		CachedPCGGraph = nullptr;
		SerializedPropertyDelta.Empty();
		DeltaCollateralPaths.Empty();
	}
}
#endif

#pragma endregion

#pragma region UPCGExActorCollection::RebuildPropertiesFromActorComponents

namespace PCGExActorCollectionInternal
{
	// Resolve the donor actor for a given entry: prefer an explicit live instance from the
	// caller, fall back to resolving entry.DeltaSourceActor. Class match on the resolved path
	// mirrors the UpdateStaging delta-capture constraint -- a re-pointed reference must not
	// silently feed another class's properties into the schema.
	AActor* ResolveDonorActor(
		const FPCGExActorCollectionEntry& Entry,
		AActor* RepresentativeInstance,
		TSharedPtr<FStreamableHandle>& OutHandle)
	{
		if (RepresentativeInstance)
		{
			return RepresentativeInstance;
		}

		if (!Entry.DeltaSourceActor.ToSoftObjectPath().IsValid())
		{
			return nullptr;
		}

		AActor* LevelActor = PCGExCollections::ResolveLevelActor(Entry.DeltaSourceActor.ToSoftObjectPath(), OutHandle);
		if (!LevelActor)
		{
			return nullptr;
		}

		UClass* ExpectedClass = Entry.Actor.Get();
		if (ExpectedClass && !LevelActor->IsA(ExpectedClass))
		{
			PCGExHelpers::SafeReleaseHandle(OutHandle);
			return nullptr;
		}
		return LevelActor;
	}
}

bool UPCGExActorCollection::GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
{
	if (!StructType || !StructType->IsChildOf(FPCGExActorCollectionGlobals::StaticStruct()))
	{
		return Super::GetTypeGlobalsInternal(StructType, OutGlobals);
	}

	FPCGExActorCollectionGlobals& Out = static_cast<FPCGExActorCollectionGlobals&>(OutGlobals);
	Out.BoundsEvaluator = BoundsEvaluator;
	Out.SchemaMergePolicy = SchemaMergePolicy;
	return true;
}

void UPCGExActorCollection::RebuildPropertiesFromActorComponents(
	EPCGExSchemaMergePolicy Policy,
	TArrayView<AActor*> RepresentativeInstances)
{
	RebuildActorPropertiesFromComponents(this, Policy, RepresentativeInstances);
}

void UPCGExActorCollection::RebuildActorPropertiesFromComponents(
	UPCGExAssetCollection* Host,
	EPCGExSchemaMergePolicy Policy,
	TArrayView<AActor*> RepresentativeInstances)
{
	if (!Host)
	{
		return;
	}

	// Remap entries before the downstream per-entry SyncToSchema -- otherwise SyncToSchema's
	// HeaderId index aliases collided entries and per-entry authored values silently fall
	// through to the schema default during the canonical rebuild below.
	Host->SyncPropertySchemaAndRemapEntries();

	// Source ordering under FirstWins / StrictTypeMatch:
	//   1. Inherited-defaults aggregate (per-BP-class CDO views; asset-default fallback when
	//      classes disagree) -- highest priority.
	//   2. Per-actor effective schemas (the donor's resolved view including its own overrides).
	//   3. Existing CollectionProperties -- lowest priority, survives only for manual-only entries.

	// Per-class chain views captured during the actor scan. Aggregated after the loop so the
	// inherited slot is computed before Sources is composed in priority order.
	TMap<UClass*, TArray<FInstancedStruct>> InheritedByClass;
	TMap<UClass*, TArray<FInstancedStruct>> AssetDefaultsByClass;

	const int32 NumEntriesTotal = Host->NumEntries();

	// Per-entry effective schemas, parallel to raw entry indices (empty = no donor).
	TArray<TArray<FInstancedStruct>> EntryCompSchemas;
	EntryCompSchemas.SetNum(NumEntriesTotal);

	// Loaded-level handle holders, kept alive across the merge call so donor pointers stay
	// valid. One handle per entry that needed a level load.
	TArray<TSharedPtr<FStreamableHandle>> LevelHandles;
	LevelHandles.Reserve(NumEntriesTotal);

	// Only actor-typed leaf entries contribute donors; all entries re-sync below.
	Host->ForEachEntry([&](const FPCGExAssetCollectionEntry* BaseEntry, const int32 i)
	{
		if (BaseEntry->bIsSubCollection || !BaseEntry->IsType(PCGExAssetCollection::TypeIds::Actor))
		{
			return;
		}

		const FPCGExActorCollectionEntry& Entry = *static_cast<const FPCGExActorCollectionEntry*>(BaseEntry);

		AActor* Instance = RepresentativeInstances.IsValidIndex(i) ? RepresentativeInstances[i] : nullptr;
		TSharedPtr<FStreamableHandle> Handle;
		AActor* Donor = PCGExActorCollectionInternal::ResolveDonorActor(Entry, Instance, Handle);
		if (Handle.IsValid())
		{
			LevelHandles.Add(Handle);
		}

		if (!Donor)
		{
			return;
		}

		TArray<FInstancedStruct> CompSchema = UPCGExPropertyCollectionComponent::ExtractSchemaFromActor(Donor);
		if (CompSchema.IsEmpty())
		{
			return;
		}

		// First donor of each class wins; subsequent donors share the same CDO so re-extracting
		// is redundant.
		UClass* DonorClass = Donor->GetClass();
		if (!InheritedByClass.Contains(DonorClass))
		{
			if (UPCGExPropertyCollectionComponent* DonorComp = UPCGExPropertyCollectionComponent::FindOnActor(Donor))
			{
				InheritedByClass.Add(DonorClass, DonorComp->BuildInheritedSchema());
				AssetDefaultsByClass.Add(DonorClass, DonorComp->BuildAssetDefaultSchema());
			}
		}

		EntryCompSchemas[i] = MoveTemp(CompSchema);
	});

	TArray<TConstArrayView<FInstancedStruct>> InheritedViews;
	InheritedViews.Reserve(InheritedByClass.Num());
	for (const TPair<UClass*, TArray<FInstancedStruct>>& Pair : InheritedByClass)
	{
		InheritedViews.Emplace(Pair.Value);
	}
	TArray<TConstArrayView<FInstancedStruct>> AssetDefaultViews;
	AssetDefaultViews.Reserve(AssetDefaultsByClass.Num());
	for (const TPair<UClass*, TArray<FInstancedStruct>>& Pair : AssetDefaultsByClass)
	{
		AssetDefaultViews.Emplace(Pair.Value);
	}
	TArray<FInstancedStruct> InheritedAggregate = PCGExProperties::AggregateAgreedValuesByName(InheritedViews, AssetDefaultViews);

	TArray<TArray<FInstancedStruct>> Sources;
	Sources.Reserve(2 + NumEntriesTotal);

	// Parallel to the raw entry indices; INDEX_NONE means "this entry contributed no source".
	TArray<int32> EntrySourceIdx;
	EntrySourceIdx.Init(INDEX_NONE, NumEntriesTotal);

	Sources.Add(MoveTemp(InheritedAggregate));
	for (int32 i = 0; i < NumEntriesTotal; ++i)
	{
		if (!EntryCompSchemas[i].IsEmpty())
		{
			EntrySourceIdx[i] = Sources.Add(MoveTemp(EntryCompSchemas[i]));
		}
	}
	Sources.Add(Host->CollectionProperties.BuildSchema());

	// Nothing authored anywhere: leave existing state untouched (avoids no-op churn).
	bool bAnySource = false;
	for (const TArray<FInstancedStruct>& S : Sources)
	{
		if (!S.IsEmpty())
		{
			bAnySource = true;
			break;
		}
	}
	if (!bAnySource)
	{
		for (TSharedPtr<FStreamableHandle>& H : LevelHandles)
		{
			PCGExHelpers::SafeReleaseHandle(H);
		}
		return;
	}

	const PCGExProperties::FSchemaMergeResult MergeResult = PCGExProperties::MergeSchemas(Sources, Policy);
	PCGExProperties::LogSchemaConflicts(MergeResult, Host);
	PCGExProperties::ApplyMergeResultToSchemas(Host->CollectionProperties, MergeResult.Merged);

	TArray<FInstancedStruct> CanonicalSchema = Host->CollectionProperties.BuildSchema();

	// SyncToSchema preserves overrides via HeaderId match (ALL entries share the collection
	// schema); per-source contributors then get authored values written + enabled.
	Host->ForEachEntry([&](FPCGExAssetCollectionEntry* BaseEntry, const int32 i)
	{
		BaseEntry->PropertyOverrides.SyncToSchema(CanonicalSchema);

		const int32 SourceIdx = EntrySourceIdx[i];
		if (SourceIdx == INDEX_NONE)
		{
			return;
		}

		const TArray<int32>& LocalToMerged = MergeResult.SourceToMergedIdx[SourceIdx];
		const TArray<FInstancedStruct>& SourceProps = Sources[SourceIdx];

		for (int32 LocalIdx = 0; LocalIdx < SourceProps.Num(); ++LocalIdx)
		{
			const int32 MergedIdx = LocalToMerged[LocalIdx];
			if (MergedIdx == INDEX_NONE || !BaseEntry->PropertyOverrides.Overrides.IsValidIndex(MergedIdx))
			{
				continue;
			}

			FPCGExPropertyOverrideEntry& Slot = BaseEntry->PropertyOverrides.Overrides[MergedIdx];
			Slot.Value = SourceProps[LocalIdx];
			Slot.bEnabled = true;

			// FInstancedStruct came in from the donor's component with that component's
			// identity bits; restamp so the override's inner property matches the canonical
			// schema's identity (lets future SyncToSchema preserve values via HeaderId).
			if (FPCGExProperty* OverrideProp = Slot.GetPropertyMutable())
			{
				if (const FPCGExProperty* SchemaProp = CanonicalSchema[MergedIdx].GetPtr<FPCGExProperty>())
				{
					OverrideProp->PropertyName = SchemaProp->PropertyName;
#if WITH_EDITOR
					OverrideProp->HeaderId = SchemaProp->HeaderId;
#endif
				}
			}
		}
	});

	// This is a fourth schema-sync funnel: it replaces CollectionProperties wholesale and rolls its
	// own per-entry loop above, so it never reaches SyncPropertyOverridesToEntries. Category rows
	// must be re-synced here too or they silently drift out of the schema on Actor/Omni rebuilds.
	Host->SyncCategoryOverridesToSchema(CanonicalSchema);

	Host->RebuildPropertyRegistry();

	for (TSharedPtr<FStreamableHandle>& H : LevelHandles)
	{
		PCGExHelpers::SafeReleaseHandle(H);
	}
}

#pragma endregion

#if WITH_EDITOR
void UPCGExActorCollection::EDITOR_OnPostStagingRebuild()
{
	Super::EDITOR_OnPostStagingRebuild();

	// User-driven editor rebuilds only -- the non-editor RebuildStagingData variant used by
	// the level exporter doesn't fire this hook, so embedded actor collections built during
	// PCGDataAsset export don't double-scan.
	RebuildPropertiesFromActorComponents(SchemaMergePolicy);
}

void UPCGExActorCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Handle Blueprint assets
		if (SelectedAsset.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
		{
			const UBlueprint* Blueprint = Cast<UBlueprint>(SelectedAsset.GetAsset());
			if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
			{
				continue;
			}

			TSoftClassPtr<AActor> ActorClass = TSoftClassPtr<AActor>(Blueprint->GeneratedClass.Get());

			bool bAlreadyExists = false;
			for (const FPCGExActorCollectionEntry& ExistingEntry : Entries)
			{
				if (ExistingEntry.Actor == ActorClass)
				{
					bAlreadyExists = true;
					break;
				}
			}

			if (bAlreadyExists)
			{
				continue;
			}

			FPCGExActorCollectionEntry Entry = FPCGExActorCollectionEntry();
			Entry.Actor = ActorClass;

			Entries.Add(Entry);
		}
	}
}
#endif
