// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionsHelpers.h"
#include "PCGExLog.h"

#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Collections/PCGExMeshCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif
#include "Helpers/PCGHelpers.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "MeshSelectors/PCGSkinnedMeshSelector.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExMicroEntryPickerOperation.h"
#include "Selectors/PCGExSelectorClassic.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorSharedData.h"

namespace PCGExCollections
{
	// Synthesize a transient Classic factory mirroring the Legacy Details structs, so downstream
	// code reads ActiveFactory->BaseConfig regardless of mode. Entry (micro) details must be
	// mirrored too: legacy consumers always pass this factory, and CreateMicroOperation
	// dispatches on BaseConfig.SubDistribution.
	UPCGExSelectorFactoryData* BuildLegacyFactory(FPCGExContext* InContext, const FPCGExAssetDistributionDetails& InDetails, const FPCGExMicroCacheDistributionDetails& InEntryDetails)
	{
		UPCGExSelectorClassicFactoryData* Factory = InContext->ManagedObjects->New<UPCGExSelectorClassicFactoryData>();

		Factory->Config.Mode = InDetails.Distribution;
		Factory->Config.IndexConfig = InDetails.IndexSettings;
		Factory->BaseConfig.SubDistribution = InEntryDetails;
		Factory->BaseConfig.SeedComponents = InDetails.SeedComponents;
		Factory->BaseConfig.LocalSeed = InDetails.LocalSeed;
		Factory->BaseConfig.bUseCategories = InDetails.bUseCategories;
		Factory->BaseConfig.Category = InDetails.Category;
		Factory->BaseConfig.MissingCategoryBehavior = InDetails.MissingCategoryBehavior;

		return Factory;
	}

	const PCGExMeshCollection::FMicroCache* GetRefreshableMicroCache(const FPCGExAssetCollectionEntry* InEntry)
	{
		const PCGExAssetCollection::FMicroCache* MicroCache = InEntry->MicroCache.Get();
		if (!MicroCache || MicroCache->GetTypeId() != PCGExAssetCollection::TypeIds::Mesh || MicroCache->IsEmpty())
		{
			return nullptr;
		}
		return static_cast<const PCGExMeshCollection::FMicroCache*>(MicroCache);
	}

	void ForwardCollectionMap(FPCGContext* InContext)
	{
		TArray<FPCGTaggedData> MapSources = InContext->InputData.GetInputsByPin(Labels::SourceCollectionMapLabel);
		for (const FPCGTaggedData& TaggedData : MapSources)
		{
			FPCGTaggedData& TaggedDataCopy = InContext->OutputData.TaggedData.Emplace_GetRef();
			TaggedDataCopy.Data = TaggedData.Data;
			TaggedDataCopy.Tags.Append(TaggedData.Tags);
			TaggedDataCopy.Pin = Labels::OutputCollectionMapLabel;
		}
	}

	void FinalizeSpawnedActor(AActor* InActor, UPCGManagedActors* InManagedActors, bool bIsPreview)
	{
		if (!InActor)
		{
			return;
		}

		InActor->Tags.AddUnique(PCGHelpers::DefaultPCGActorTag);

		if (!bIsPreview)
		{
			InActor->Modify();
			(void)InActor->MarkPackageDirty();
			if (ULevel* Level = InActor->GetLevel())
			{
				Level->Modify();
				(void)Level->MarkPackageDirty();
			}
		}

		if (InManagedActors)
		{
			InManagedActors->GetMutableGeneratedActors().Add(InActor);
		}
	}

	AActor* ResolveTargetActor(FPCGExContext* InContext, const FSoftObjectPath& InPath, TMap<FSoftObjectPath, TWeakObjectPtr<AActor>>& InOutCache)
	{
		if (!InPath.IsNull())
		{
			if (const TWeakObjectPtr<AActor>* Cached = InOutCache.Find(InPath))
			{
				if (AActor* Live = Cached->Get())
				{
					return Live;
				}
			}
			if (AActor* Resolved = Cast<AActor>(InPath.ResolveObject()))
			{
				InOutCache.Add(InPath, Resolved);
				return Resolved;
			}
		}
		return InContext->GetTargetActor(nullptr);
	}

	// Selector Helper Implementation

	FSelectorHelper::FSelectorHelper(UPCGExAssetCollection* InCollection, const FPCGExAssetDistributionDetails& InDetails)
		: Collection(InCollection)
		  , Details(InDetails)
	{
	}

	// Init resolves the active factory (External or transient-from-Legacy), then creates
	// picker operations for Cache->Main and each named category. Hot-path dispatch after
	// Init is: category map lookup -> op->Pick -> GetEntryRaw -> subcollection recursion.
	bool FSelectorHelper::Init(const TSharedRef<PCGExData::FFacade>& InDataFacade, const UPCGExSelectorFactoryData* ExternalFactory)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FSelectorHelper::Init);

		Cache = Collection->PinCache();

		if (!Cache || Cache->IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, InDataFacade->GetContext(), FTEXT("FSelectorHelper got an empty Collection."));
			return false;
		}

		// Subcollection hops resolve their host's cache per point -- pin the whole reachable tree.
		Collection->PinCaches(CachePins);

		FPCGExContext* Ctx = InDataFacade->GetContext();

		ActiveFactory = ExternalFactory;
		if (!ActiveFactory)
		{
			return false;
		}

		const FPCGExSelectorFactoryBaseConfig& BaseConfig = ActiveFactory->BaseConfig;

		if (BaseConfig.bUseCategories)
		{
			CategoryGetter = BaseConfig.Category.GetValueSetting();
			if (!CategoryGetter->Init(InDataFacade))
			{
				return false;
			}
		}

		// Route every shared-data request through BuildSharedData. If a cache is wired, the cache
		// deduplicates across facades; otherwise we call directly (one-shot, non-cached).
		// Selectors that don't override BuildSharedData get nullptr either way.
		auto ObtainSharedData = [&](const PCGExAssetCollection::FCategory* Target) -> TSharedPtr<FSelectorSharedData>
		{
			return SharedDataCache
				? SharedDataCache->GetOrBuild(ActiveFactory, Collection, Target)
				: ActiveFactory->BuildSharedData(Collection, Target);
		};

		MainPickerOp = ActiveFactory->CreateEntryOperation(Ctx);
		if (!MainPickerOp)
		{
			return false;
		}
		MainPickerOp->SharedData = ObtainSharedData(Cache->Main.Get());
		if (!MainPickerOp->PrepareForData(Ctx, InDataFacade, Cache->Main.Get(), Collection))
		{
			return false;
		}

		if (BaseConfig.bUseCategories)
		{
			// Parallel array indexed by Cache->CategoryNameToIndex. Slots stay null when the
			// op fails PrepareForData; ResolvePickerForPoint treats null as "use Main or skip".
			const int32 NumCategories = Cache->Categories.Num();
			CategoryPickerOpsByIndex.SetNum(NumCategories);
			for (int32 i = 0; i < NumCategories; ++i)
			{
				PCGExAssetCollection::FCategory* CategoryPtr = Cache->Categories[i].Get();
				TSharedPtr<FPCGExEntryPickerOperation> Op = ActiveFactory->CreateEntryOperation(Ctx);
				if (!Op)
				{
					continue;
				}
				Op->SharedData = ObtainSharedData(CategoryPtr);
				if (Op->PrepareForData(Ctx, InDataFacade, CategoryPtr, Collection))
				{
					CategoryPickerOpsByIndex[i] = Op;
				}
			}

			// Needed by the blank-key path regardless of MissingCategoryBehavior, so it cannot be
			// gated on that arm. Left null when the pool is empty: "no uncategorized entries" must
			// degrade to a miss, not to a picker that draws nothing.
			if (PCGExAssetCollection::FCategory* UncategorizedPtr = Cache->Uncategorized.Get();
				UncategorizedPtr && !UncategorizedPtr->IsEmpty())
			{
				if (TSharedPtr<FPCGExEntryPickerOperation> Op = ActiveFactory->CreateEntryOperation(Ctx))
				{
					Op->SharedData = ObtainSharedData(UncategorizedPtr);
					if (Op->PrepareForData(Ctx, InDataFacade, UncategorizedPtr, Collection))
					{
						UncategorizedPickerOp = Op;
					}
				}
			}
		}

		// Sync the inline Details struct with the effective factory config so callers can
		// read Helper->Details.SeedComponents / LocalSeed without knowing which mode is active.
		Details.SeedComponents = BaseConfig.SeedComponents;
		Details.LocalSeed = BaseConfig.LocalSeed;

		return true;
	}

	// Resolve which picker to use for this point. With categories in use a blank key is a real
	// selector value meaning "the uncategorized group" -- it must never widen to the whole pool, or
	// blank-keyed points would start receiving categorized entries. When the collection has no
	// uncategorized entries there is nothing to ask for, so it degrades to a miss and
	// MissingCategoryBehavior decides -- same as a key naming a category that doesn't exist.
	// Returns nullptr when the result should be an empty FPCGExEntryAccessResult.
	const FPCGExEntryPickerOperation* FSelectorHelper::ResolvePickerForPoint(int32 PointIndex, int32& OutCategorySlot) const
	{
		OutCategorySlot = CategorySlot_Main;

		if (!CategoryGetter)
		{
			return MainPickerOp.Get();
		}

		auto UseUncategorized = [this, &OutCategorySlot]() -> const FPCGExEntryPickerOperation*
		{
			if (!UncategorizedPickerOp)
			{
				return nullptr;
			}
			OutCategorySlot = CategorySlot_Uncategorized;
			return UncategorizedPickerOp.Get();
		};

		const FName CategoryKey = CategoryGetter->Read(PointIndex);

		if (!CategoryKey.IsNone())
		{
			if (const int32* IdxPtr = Cache->CategoryNameToIndex.Find(CategoryKey))
			{
				if (CategoryPickerOpsByIndex.IsValidIndex(*IdxPtr))
				{
					if (const TSharedPtr<FPCGExEntryPickerOperation>& Op = CategoryPickerOpsByIndex[*IdxPtr])
					{
						OutCategorySlot = *IdxPtr;
						return Op.Get();
					}
				}
			}
		}
		else if (const FPCGExEntryPickerOperation* Op = UseUncategorized())
		{
			return Op;
		}

		switch (ActiveFactory->BaseConfig.MissingCategoryBehavior)
		{
		case EPCGExMissingCategoryBehavior::UseMain:
			OutCategorySlot = CategorySlot_Main;
			return MainPickerOp.Get();
		case EPCGExMissingCategoryBehavior::UseUncategorized:
			return UseUncategorized();
		case EPCGExMissingCategoryBehavior::Skip:
			return nullptr;
		default:
			// Unresolvable enumerator -- fail loud rather than silently aliasing another arm.
			checkNoEntry();
			return nullptr;
		}
	}

	const PCGExAssetCollection::FCategory* FSelectorHelper::GetPool(const int32 CategorySlot) const
	{
		if (CategorySlot == CategorySlot_Uncategorized)
		{
			return Cache->Uncategorized.Get();
		}
		return Cache->Categories.IsValidIndex(CategorySlot) ? Cache->Categories[CategorySlot].Get() : Cache->Main.Get();
	}

	// Scratch slots parallel the op layout (Main + Uncategorized + CategoryPickerOpsByIndex). Ops
	// that don't override CreateScratchForScope leave their slot null; a fully-null set collapses
	// to nullptr so consumers can skip the routing entirely.
	TSharedPtr<FSelectorScratches> FSelectorHelper::CreateScratches(const int32 MaxPointsInScope) const
	{
		bool bAny = false;
		TSharedPtr<FSelectorScratches> Result = MakeShared<FSelectorScratches>();

		if (MainPickerOp)
		{
			Result->Main = MainPickerOp->CreateScratchForScope(MaxPointsInScope);
			bAny |= Result->Main.IsValid();
		}

		if (UncategorizedPickerOp)
		{
			Result->Uncategorized = UncategorizedPickerOp->CreateScratchForScope(MaxPointsInScope);
			bAny |= Result->Uncategorized.IsValid();
		}

		Result->ByCategory.SetNum(CategoryPickerOpsByIndex.Num());
		for (int32 i = 0; i < CategoryPickerOpsByIndex.Num(); ++i)
		{
			if (const TSharedPtr<FPCGExEntryPickerOperation>& Op = CategoryPickerOpsByIndex[i])
			{
				Result->ByCategory[i] = Op->CreateScratchForScope(MaxPointsInScope);
				bAny |= Result->ByCategory[i].IsValid();
			}
		}

		return bAny ? Result : nullptr;
	}

	bool FSelectorHelper::AnyPickerWantsPreResolve() const
	{
		if (MainPickerOp && MainPickerOp->WantsPreResolve())
		{
			return true;
		}
		if (UncategorizedPickerOp && UncategorizedPickerOp->WantsPreResolve())
		{
			return true;
		}
		for (const TSharedPtr<FPCGExEntryPickerOperation>& Op : CategoryPickerOpsByIndex)
		{
			if (Op && Op->WantsPreResolve())
			{
				return true;
			}
		}
		return false;
	}

	void FSelectorHelper::BeginPreResolve(const int32 NumPoints) const
	{
		if (MainPickerOp && MainPickerOp->WantsPreResolve())
		{
			MainPickerOp->BeginPreResolve(NumPoints);
		}
		if (UncategorizedPickerOp && UncategorizedPickerOp->WantsPreResolve())
		{
			UncategorizedPickerOp->BeginPreResolve(NumPoints);
		}
		for (const TSharedPtr<FPCGExEntryPickerOperation>& Op : CategoryPickerOpsByIndex)
		{
			if (Op && Op->WantsPreResolve())
			{
				Op->BeginPreResolve(NumPoints);
			}
		}
	}

	FPCGExEntryPickerOperation* FSelectorHelper::ResolvePreResolveOp(const int32 PointIndex, const FSelectorScratches* Scratches, FPCGExPickerScratchBase*& OutScratch) const
	{
		int32 CategorySlot = CategorySlot_Main;
		// Ops are owned mutably (TSharedPtr members); the const routing method is reused.
		FPCGExEntryPickerOperation* Op = const_cast<FPCGExEntryPickerOperation*>(ResolvePickerForPoint(PointIndex, CategorySlot));
		if (!Op || !Op->WantsPreResolve())
		{
			return nullptr;
		}
		OutScratch = Scratches ? Scratches->GetSlot(CategorySlot) : nullptr;
		return Op;
	}

	void FSelectorHelper::PreResolveFirstChoice(const int32 PointIndex, const int32 Seed, const FSelectorScratches* Scratches) const
	{
		FPCGExPickerScratchBase* Scratch = nullptr;
		if (FPCGExEntryPickerOperation* Op = ResolvePreResolveOp(PointIndex, Scratches, Scratch))
		{
			Op->PreResolveFirstChoice(PointIndex, Seed, Scratch);
		}
	}

	void FSelectorHelper::CommitPreResolve(const int32 PointIndex, const int32 Seed, const FSelectorScratches* Scratches) const
	{
		FPCGExPickerScratchBase* Scratch = nullptr;
		if (FPCGExEntryPickerOperation* Op = ResolvePreResolveOp(PointIndex, Scratches, Scratch))
		{
			Op->CommitPreResolve(PointIndex, Seed, Scratch);
		}
	}

	// Entry picking: resolve the active picker (category-aware) -> pick a raw entries index
	// -> resolve entry -> handle subcollection recursion via the "fallback to WeightedRandom"
	// policy (matches current behavior when the picked entry is a subcollection).
	FPCGExEntryAccessResult FSelectorHelper::GetEntry(int32 PointIndex, int32 Seed, const bool bFlattenSubCollections, const FSelectorScratches* Scratches) const
	{
		int32 CategorySlot = -1;
		const FPCGExEntryPickerOperation* Op = ResolvePickerForPoint(PointIndex, CategorySlot);
		if (!Op)
		{
			return FPCGExEntryAccessResult{};
		}

		FPCGExPickerScratchBase* Scratch = Scratches ? Scratches->GetSlot(CategorySlot) : nullptr;

		const int32 Raw = Op->Pick(PointIndex, Seed, Scratch);
		FPCGExEntryAccessResult Result = Collection->GetEntryRaw(Raw);
		if (Result && (!bFlattenSubCollections && Result.Entry->HasValidSubCollection()))
		{
			// The nested pick reports its own pool -- the root's slot didn't produce this entry.
			return Result.Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed);
		}
		Result.Pool = GetPool(CategorySlot);
		return Result;
	}

	FPCGExEntryAccessResult FSelectorHelper::GetEntry(int32 PointIndex, int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags, const bool bFlattenSubCollections, const FSelectorScratches* Scratches) const
	{
		if (TagInheritance == 0)
		{
			return GetEntry(PointIndex, Seed, bFlattenSubCollections, Scratches);
		}

		int32 CategorySlot = -1;
		const FPCGExEntryPickerOperation* Op = ResolvePickerForPoint(PointIndex, CategorySlot);
		if (!Op)
		{
			return FPCGExEntryAccessResult{};
		}

		FPCGExPickerScratchBase* Scratch = Scratches ? Scratches->GetSlot(CategorySlot) : nullptr;

		const int32 Raw = Op->Pick(PointIndex, Seed, Scratch);
		FPCGExEntryAccessResult Result = Collection->GetEntryRaw(Raw, TagInheritance, OutTags);
		if (Result && (!bFlattenSubCollections && Result.Entry->HasValidSubCollection()))
		{
			// The nested pick reports its own pool -- the root's slot didn't produce this entry.
			return Result.Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed, TagInheritance, OutTags);
		}
		Result.Pool = GetPool(CategorySlot);
		return Result;
	}

	// MicroDistribution Helper Implementation

	FMicroSelectorHelper::FMicroSelectorHelper(const FPCGExMicroCacheDistributionDetails& InDetails)
		: Details(InDetails)
	{
	}

	// Legacy mode: synthesize a transient Classic factory with BaseConfig.EntryDistribution
	// populated from the inline micro Details. External mode: use the caller-provided factory.
	// Either way, the factory's CreateMicroOperation dispatches on EntryDistribution.Distribution.
	bool FMicroSelectorHelper::Init(const TSharedRef<PCGExData::FFacade>& InDataFacade, const UPCGExSelectorFactoryData* ExternalFactory)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FMicroSelectorHelper::Init);

		FPCGExContext* Ctx = InDataFacade->GetContext();
		if (!Ctx->GetWorkHandle().IsValid())
		{
			return false;
		}

		const UPCGExSelectorFactoryData* Factory = ExternalFactory;
		if (!Factory)
		{
			UPCGExSelectorClassicFactoryData* Transient = Ctx->ManagedObjects->New<UPCGExSelectorClassicFactoryData>();
			Transient->BaseConfig.SubDistribution = Details;
			Factory = Transient;
		}

		PickerOp = Factory->CreateMicroOperation(Ctx);
		if (!PickerOp)
		{
			return false;
		}
		return PickerOp->PrepareForData(Ctx, InDataFacade);
	}

	int32 FMicroSelectorHelper::GetPick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const
	{
		return PickerOp ? PickerOp->Pick(InMicroCache, PointIndex, Seed) : -1;
	}

	// --- Pick Packer ---
	// Encodes collection identity + entry index + secondary index into a single uint64.
	// IMPORTANT: InIndex is a RAW Entries array index, not a cache-adjusted index.
	// The unpacker resolves via GetEntryRaw() to bypass cache indirection.
	// CollectionGUID is a persistent uint32 on each UPCGExAssetCollection, deterministic
	// across sessions and nodes. This enables merging collection maps from multiple sources.
	// SecondaryIndex is stored +1 so that 0 can represent "no secondary" (unpacker subtracts 1).

	FPickPacker::FPickPacker(FPCGContext* InContext)
	{
		// Kept for API backward compatibility. GUID-based scheme no longer needs context.
	}

	// Registers the given collection and every host reachable from it via FCache::FlatHosts.
	// A single top-level call therefore covers the entire nested-collection tree. The write
	// lock is held for the full batch to amortize the cost of one acquire across K inserts.
	// Re-entrant / idempotent: collections already mapped are skipped.
	void FPickPacker::RegisterCollection(UPCGExAssetCollection* InCollection)
	{
		if (!InCollection)
		{
			return;
		}

		const TArray<TObjectPtr<UPCGExAssetCollection>>& FlatHosts = InCollection->GetFlatHosts();

		FWriteScopeLock WriteScopeLock(AssetCollectionsLock);
		CollectionMap.Reserve(CollectionMap.Num() + FlatHosts.Num());
		for (const TObjectPtr<UPCGExAssetCollection>& Host : FlatHosts)
		{
			if (UPCGExAssetCollection* H = Host.Get())
			{
				CollectionMap.FindOrAdd(H, H->GetCollectionGUID());
			}
		}
	}

	// Writes the collection→GUID mapping as rows in an attribute set.
	// Each row has a CollectionIdx (the collection's GUID used in hashes) and a CollectionPath
	// (the soft path needed to reload the collection on the consumption side).
	void FPickPacker::PackToDataset(const UPCGParamData* InAttributeSet)
	{
		TArray<const UPCGExAssetCollection*> Collections;
		{
			FReadScopeLock ReadScopeLock(AssetCollectionsLock);
			CollectionMap.GetKeys(Collections);
		}
		AppendMapRows(InAttributeSet->Metadata, Collections);
	}

	void FPickPacker::AppendMapRows(UPCGMetadata* InMetadata, TConstArrayView<const UPCGExAssetCollection*> InCollections)
	{
		if (!InMetadata || InCollections.IsEmpty())
		{
			return;
		}

		FPCGMetadataAttribute<int32>* CollectionIdx = InMetadata->FindOrCreateAttribute<int32>(
			Labels::Tag_CollectionIdx, 0,
			false, true, true);
		FPCGMetadataAttribute<FSoftObjectPath>* CollectionPath = InMetadata->FindOrCreateAttribute<FSoftObjectPath>(
			Labels::Tag_CollectionPath, FSoftObjectPath(),
			false, true, true);

		if (!CollectionIdx || !CollectionPath)
		{
			return;
		}

		TMap<uint32, FSoftObjectPath> Seen;
		const int64 NumExisting = InMetadata->GetItemCountForChild();
		Seen.Reserve(NumExisting + InCollections.Num());
		for (int64 i = 0; i < NumExisting; i++)
		{
			Seen.Add(static_cast<uint32>(CollectionIdx->GetValueFromItemKey(i)), CollectionPath->GetValueFromItemKey(i));
		}

		for (const UPCGExAssetCollection* Collection : InCollections)
		{
			if (!Collection)
			{
				continue;
			}

			const uint32 GUID = Collection->GetCollectionGUID();
			const FSoftObjectPath Path = FSoftObjectPath(Collection);
			if (const FSoftObjectPath* Existing = Seen.Find(GUID))
			{
				// Two DISTINCT collections on one GUID (out-of-band asset copy): picks against the dropped
				// one would resolve into the survivor's entries. Cannot be made correct here -- be loud.
				if (*Existing != Path)
				{
					UE_LOG(LogPCGEx, Error, TEXT("Collection GUID collision: '%s' and '%s' share GUID %u -- picks into the former will resolve against the latter. Re-save a duplicate of one to re-mint its GUID."),
					       *Path.ToString(), *Existing->ToString(), GUID);
				}
				continue;
			}
			Seen.Add(GUID, Path);

			const int64 Key = InMetadata->AddEntry();
			CollectionIdx->SetValue(Key, static_cast<int32>(GUID));
			CollectionPath->SetValue(Key, Path);
		}
	}

	void FPickPacker::AppendMapRows(UPCGMetadata* InMetadata, UPCGExAssetCollection* InCollection)
	{
		if (!InCollection)
		{
			return;
		}

		const TArray<TObjectPtr<UPCGExAssetCollection>>& FlatHosts = InCollection->GetFlatHosts();
		TArray<const UPCGExAssetCollection*> Collections;
		Collections.Reserve(FlatHosts.Num());
		for (const TObjectPtr<UPCGExAssetCollection>& Host : FlatHosts)
		{
			Collections.Add(Host.Get());
		}
		AppendMapRows(InMetadata, Collections);
	}

	// --- Pick Unpacker ---
	// Reverses the packing: reads CollectionIdx→CollectionPath pairs from the attribute set,
	// loads all referenced collections, then provides UnpackHash/ResolveEntry to decode
	// per-point uint64 hashes back into (Collection, EntryIndex, SecondaryIndex).

	bool FPickUnpacker::UnpackDataset(FPCGContext* InContext, const UPCGParamData* InAttributeSet)
	{
		const UPCGMetadata* Metadata = InAttributeSet->Metadata;
		TUniquePtr<FPCGAttributeAccessorKeysEntries> Keys = MakeUnique<FPCGAttributeAccessorKeysEntries>(Metadata);

		const int32 NumEntries = Keys->GetNum();
		if (NumEntries == 0)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Collection map is empty."));
			return false;
		}

		CollectionMap.Reserve(CollectionMap.Num() + NumEntries);

		const FPCGMetadataAttribute<int32>* CollectionIdx = InAttributeSet->Metadata->GetConstTypedAttribute<int32>(Labels::Tag_CollectionIdx);
		const FPCGMetadataAttribute<FSoftObjectPath>* CollectionPath = InAttributeSet->Metadata->GetConstTypedAttribute<FSoftObjectPath>(Labels::Tag_CollectionPath);

		if (!CollectionIdx || !CollectionPath)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Missing required attributes, or unsupported type."));
			return false;
		}

		{
			TSharedPtr<TSet<FSoftObjectPath>> CollectionPaths = MakeShared<TSet<FSoftObjectPath>>();

			for (int32 i = 0; i < NumEntries; i++)
			{
				CollectionPaths->Add(CollectionPath->GetValueFromItemKey(i));
			}

#if WITH_EDITOR
			FPCGDynamicTrackingHelper DynamicTracking;
			DynamicTracking.EnableAndInitialize(InContext, CollectionPaths->Num());
			for (const FSoftObjectPath& Path : *CollectionPaths.Get())
			{
				DynamicTracking.AddToTracking(FPCGSelectionKey::CreateFromPath(Path), /*bIsCulled=*/ false);
			}
			DynamicTracking.Finalize(InContext);
#endif

			// Append (not assign): UnpackDataset accumulates -- UnpackPin calls it once per param data on the
			// pin, and CollectionMap grows across calls. The keep-alive wrappers must accumulate in lockstep,
			// or every batch but the last loses its member-anchored keep-alive (this path has no FPCGExContext
			// to anchor them on, so the member array IS the keep-alive).
			CollectionsHandles.Append(PCGExHelpers::LoadAndCacheBlocking_AnyThread(CollectionPaths));
		}

		for (int32 i = 0; i < NumEntries; i++)
		{
			int32 Idx = CollectionIdx->GetValueFromItemKey(i);

			TSoftObjectPtr<UPCGExAssetCollection> CollectionSoftPtr(CollectionPath->GetValueFromItemKey(i));
			UPCGExAssetCollection* Collection = CollectionSoftPtr.Get();

			if (!Collection)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some collections could not be loaded."));
				return false;
			}

			if (CollectionMap.Contains(Idx))
			{
				if (CollectionMap[Idx] == Collection)
				{
					continue;
				}

				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Collection Idx collision."));
				return false;
			}

			CollectionMap.Add(Idx, Collection);
			NumUniqueEntries += Collection->GetValidEntryNum();
		}

		return true;
	}

	void FPickUnpacker::RegisterCollectionsTo(FPickPacker& InPacker) const
	{
		for (const TPair<uint32, UPCGExAssetCollection*>& Pair : CollectionMap)
		{
			if (Pair.Value)
			{
				InPacker.RegisterCollection(Pair.Value);
			}
		}
	}

	void FPickUnpacker::UnpackPin(FPCGContext* InContext, const FName InPinLabel)
	{
		for (TArray<FPCGTaggedData> Params = InContext->InputData.GetParamsByPin(InPinLabel.IsNone() ? Labels::SourceCollectionMapLabel : InPinLabel);
		     const FPCGTaggedData& InTaggedData : Params)
		{
			const UPCGParamData* ParamData = Cast<UPCGParamData>(InTaggedData.Data);

			if (!ParamData)
			{
				continue;
			}

			if (!ParamData->Metadata->HasAttribute(Labels::Tag_CollectionIdx) || !ParamData->Metadata->HasAttribute(Labels::Tag_CollectionPath))
			{
				continue;
			}

			UnpackDataset(InContext, ParamData);
		}
	}

	// Groups points by their entry hash into FPCGMeshInstanceList partitions.
	// Each unique hash gets one instance list; points sharing the same hash share the list.
	// Used by the PCG mesh spawner pipeline to batch-instantiate identical meshes.
	// Instance-list traits.
	// Absorb the per-list-type differences between FPCGMeshInstanceList and FPCGSkinnedMeshInstanceList:
	//   - InstancesIndices vs InstancePointIndices
	//   - PointData type (UPCGBasePointData vs UPCGPointData -- UPCGPointData is a UPCGBasePointData
	//     subclass, so a Cast<> resolves at runtime to the same object; the cast survives the
	//     hierarchy change painlessly when the engine consolidates these later).
	// Adding a new instance-list type is a matter of providing one specialization below and adding
	// an explicit instantiation pair at the bottom of this file.
	template <typename T>
	struct TInstanceListTraits;

	template <>
	struct TInstanceListTraits<FPCGMeshInstanceList>
	{
		static TArray<int32>& GetIndices(FPCGMeshInstanceList& InList)
		{
			return InList.InstancesIndices;
		}

		static void SetPointData(FPCGMeshInstanceList& InList, const UPCGBasePointData* InPointData)
		{
			InList.PointData = InPointData;
		}
	};

	template <>
	struct TInstanceListTraits<FPCGSkinnedMeshInstanceList>
	{
		static TArray<int32>& GetIndices(FPCGSkinnedMeshInstanceList& InList)
		{
			return InList.InstancePointIndices;
		}

		static void SetPointData(FPCGSkinnedMeshInstanceList& InList, const UPCGBasePointData* InPointData)
		{
			InList.PointData = Cast<UPCGPointData>(InPointData);
		}
	};

	template <typename T>
	bool FPickUnpacker::BuildPartitions(const UPCGBasePointData* InPointData, TArray<T>& InstanceLists)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPickUnpacker::BuildPartitions);

		using FTraits = TInstanceListTraits<T>;

		FPCGAttributePropertyInputSelector HashSelector;
		HashSelector.Update(EntryIdxAttributeName.ToString());

		TUniquePtr<const IPCGAttributeAccessor> HashAttributeAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, HashSelector);
		TUniquePtr<const IPCGAttributeAccessorKeys> HashKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, HashSelector);

		if (!HashAttributeAccessor || !HashKeys)
		{
			return false;
		}

		TArray<int64> Hashes;
		Hashes.SetNumUninitialized(HashKeys->GetNum());

		if (!HashAttributeAccessor->GetRange<int64>(Hashes, 0, *HashKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			return false;
		}

		const int32 NumPoints = InPointData->GetNumPoints();
		const int32 SafeReserve = NumPoints / (NumUniqueEntries * 2);

		// Build partitions
		for (int32 i = 0; i < NumPoints; i++)
		{
			const uint64 EntryHash = Hashes[i];
			if (const int32* Index = IndexedPartitions.Find(EntryHash);
				!Index)
			{
				T& NewInstanceList = InstanceLists.Emplace_GetRef();
				NewInstanceList.AttributePartitionIndex = EntryHash;
				FTraits::SetPointData(NewInstanceList, InPointData);
				TArray<int32>& Indices = FTraits::GetIndices(NewInstanceList);
				Indices.Reserve(SafeReserve);
				Indices.Emplace(i);

				IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
			}
			else
			{
				FTraits::GetIndices(InstanceLists[*Index]).Emplace(i);
			}
		}

		return !IndexedPartitions.IsEmpty();
	}

	template <typename T>
	void FPickUnpacker::ReindexPartitions(const TArray<T>& InstanceLists)
	{
		IndexedPartitions.Reset();
		for (int32 i = 0; i < InstanceLists.Num(); i++)
		{
			const int64 EntryHash = InstanceLists[i].AttributePartitionIndex;
			if (EntryHash != INDEX_NONE)
			{
				IndexedPartitions.Add(EntryHash, i);
			}
		}
	}

	template <typename T>
	void FPickUnpacker::InsertEntry(const UPCGBasePointData* InPointData, const uint64 EntryHash, const int32 EntryIndex, TArray<T>& InstanceLists)
	{
		using FTraits = TInstanceListTraits<T>;

		if (const int32* Index = IndexedPartitions.Find(EntryHash);
			!Index)
		{
			T& NewInstanceList = InstanceLists.Emplace_GetRef();
			NewInstanceList.AttributePartitionIndex = EntryHash;
			FTraits::SetPointData(NewInstanceList, InPointData);
			TArray<int32>& Indices = FTraits::GetIndices(NewInstanceList);
			// Max(1) guards a collection with no valid entries; this branch only became reachable
			// once callers stopped pre-filling every partition.
			Indices.Reserve(InPointData->GetNumPoints() / FMath::Max(1, NumUniqueEntries * 2));
			Indices.Emplace(EntryIndex);

			IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
		}
		else
		{
			FTraits::GetIndices(InstanceLists[*Index]).Emplace(EntryIndex);
		}
	}

	// Explicit instantiations. Add a new set here when introducing a new instance-list type
	// alongside its TInstanceListTraits specialization above.
	template bool FPickUnpacker::BuildPartitions<FPCGMeshInstanceList>(const UPCGBasePointData*, TArray<FPCGMeshInstanceList>&);
	template bool FPickUnpacker::BuildPartitions<FPCGSkinnedMeshInstanceList>(const UPCGBasePointData*, TArray<FPCGSkinnedMeshInstanceList>&);
	template void FPickUnpacker::ReindexPartitions<FPCGMeshInstanceList>(const TArray<FPCGMeshInstanceList>&);
	template void FPickUnpacker::ReindexPartitions<FPCGSkinnedMeshInstanceList>(const TArray<FPCGSkinnedMeshInstanceList>&);
	template void FPickUnpacker::InsertEntry<FPCGMeshInstanceList>(const UPCGBasePointData*, const uint64, const int32, TArray<FPCGMeshInstanceList>&);
	template void FPickUnpacker::InsertEntry<FPCGSkinnedMeshInstanceList>(const UPCGBasePointData*, const uint64, const int32, TArray<FPCGSkinnedMeshInstanceList>&);

	void FPickUnpacker::GetCollectionsInStableOrder(TArray<const UPCGExAssetCollection*>& OutCollections) const
	{
		OutCollections.Reset(CollectionMap.Num());
		for (const TPair<uint32, UPCGExAssetCollection*>& Pair : CollectionMap)
		{
			if (Pair.Value)
			{
				OutCollections.Add(Pair.Value);
			}
		}

		OutCollections.Sort([](const UPCGExAssetCollection& A, const UPCGExAssetCollection& B)
		{
			return A.GetPathName().Compare(B.GetPathName(), ESearchCase::CaseSensitive) < 0;
		});
	}

	UPCGExAssetCollection* FPickUnpacker::UnpackHash(uint64 EntryHash, int16& OutPrimaryIndex, int16& OutSecondaryIndex)
	{
		const uint32 CollectionIdx = PickHash::GetCollectionGUID(EntryHash);
		const uint16 EntryIndex = PickHash::GetRawEntryIndex(EntryHash);
		OutSecondaryIndex = PickHash::GetSecondaryIndex(EntryHash);

		UPCGExAssetCollection** Collection = CollectionMap.Find(CollectionIdx);
		if (!Collection || !(*Collection)->IsValidIndex(EntryIndex))
		{
			return nullptr;
		}

		OutPrimaryIndex = EntryIndex;

		return *Collection;
	}

	// Resolves a packed hash back to a concrete entry.
	// Uses GetEntryRaw() because packed hashes store RAW Entries array indices,
	// not cache-adjusted indices. Using GetEntryAt() here would apply cache indirection
	// and return the wrong entry when Weight=0 entries create gaps in the cache.
	FPCGExEntryAccessResult FPickUnpacker::ResolveEntry(uint64 EntryHash, int16& OutSecondaryIndex)
	{
		int16 EntryIndex = 0;
		UPCGExAssetCollection* Collection = UnpackHash(EntryHash, EntryIndex, OutSecondaryIndex);
		if (!Collection)
		{
			return FPCGExEntryAccessResult{};
		}

		return Collection->GetEntryRaw(EntryIndex);
	}

	// Collection Source Implementation

	FCollectionSource::FCollectionSource(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
		: DataFacade(InDataFacade)
	{
	}

	bool FCollectionSource::Init(UPCGExAssetCollection* InCollection, const UPCGExSelectorFactoryData* ExternalFactory)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FCollectionSource::Init_Single);

		SingleSource = InCollection;
		Helper = MakeShared<FSelectorHelper>(InCollection, DistributionSettings);
		if (SharedDataCache)
		{
			Helper->SetSharedDataCache(SharedDataCache);
		}
		if (!Helper->Init(DataFacade.ToSharedRef(), ExternalFactory))
		{
			return false;
		}

		// Create micro helper for mesh collections
		if (InCollection->IsType(PCGExAssetCollection::TypeIds::Mesh))
		{
			MicroHelper = MakeShared<FMicroSelectorHelper>(EntryDistributionSettings);
			if (!MicroHelper->Init(DataFacade.ToSharedRef(), ExternalFactory))
			{
				return false;
			}
		}

		return true;
	}

	bool FCollectionSource::Init(const TMap<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& InMap, const TSharedPtr<TArray<PCGExValueHash>>& InKeys, const UPCGExSelectorFactoryData* ExternalFactory)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FCollectionSource::Init_Mapped);

		Keys = InKeys;
		if (!Keys)
		{
			return false;
		}

		const int32 NumElements = InMap.Num();
		Helpers.Reserve(NumElements);
		MicroHelpers.Reserve(NumElements);

		for (const TPair<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& Pair : InMap)
		{
			UPCGExAssetCollection* Collection = Pair.Value.Get();

			TSharedPtr<FSelectorHelper> NewHelper = MakeShared<FSelectorHelper>(Collection, DistributionSettings);
			if (SharedDataCache)
			{
				NewHelper->SetSharedDataCache(SharedDataCache);
			}
			if (!NewHelper->Init(DataFacade.ToSharedRef(), ExternalFactory))
			{
				continue;
			}

			Indices.Add(Pair.Key, Helpers.Add(NewHelper));

			// Create micro helper for mesh collections
			if (Collection->IsType(PCGExAssetCollection::TypeIds::Mesh))
			{
				TSharedPtr<FMicroSelectorHelper> NewMicroHelper = MakeShared<FMicroSelectorHelper>(EntryDistributionSettings);
				if (!NewMicroHelper->Init(DataFacade.ToSharedRef(), ExternalFactory))
				{
					MicroHelpers.Add(nullptr);
				}
				else
				{
					MicroHelpers.Add(NewMicroHelper);
				}
			}
			else
			{
				MicroHelpers.Add(nullptr);
			}
		}

		return !Helpers.IsEmpty();
	}

	void FCollectionSource::RegisterCollectionsTo(FPickPacker& Packer) const
	{
		if (SingleSource)
		{
			Packer.RegisterCollection(SingleSource);
			return;
		}

		for (const TSharedPtr<FSelectorHelper>& H : Helpers)
		{
			if (!H)
			{
				continue;
			}
			Packer.RegisterCollection(H->GetCollection());
		}
	}

	void FCollectionSource::RegisterSocketsTo(FSocketHelper& SocketHelper) const
	{
		if (SingleSource)
		{
			SocketHelper.RegisterCollection(SingleSource);
			return;
		}

		for (const TSharedPtr<FSelectorHelper>& H : Helpers)
		{
			if (!H)
			{
				continue;
			}
			SocketHelper.RegisterCollection(H->GetCollection());
		}
	}

	TSharedPtr<FSourceScratches> FCollectionSource::CreateScratches(const int32 MaxPointsInScope) const
	{
		TSharedPtr<FSourceScratches> Result;
		auto EnsureResult = [&Result]() -> FSourceScratches&
		{
			if (!Result)
			{
				Result = MakeShared<FSourceScratches>();
			}
			return *Result;
		};

		if (Helper)
		{
			if (TSharedPtr<FSelectorScratches> Scratches = Helper->CreateScratches(MaxPointsInScope))
			{
				EnsureResult().Single = Scratches;
			}
		}

		for (const TSharedPtr<FSelectorHelper>& MappedHelper : Helpers)
		{
			if (!MappedHelper)
			{
				continue;
			}
			if (TSharedPtr<FSelectorScratches> Scratches = MappedHelper->CreateScratches(MaxPointsInScope))
			{
				EnsureResult().ByHelper.Add(MappedHelper.Get(), Scratches);
			}
		}

		// Null when no op anywhere wants scratch -- consumers skip routing entirely.
		return Result;
	}

	bool FCollectionSource::AnyPickerWantsPreResolve() const
	{
		if (Helper && Helper->AnyPickerWantsPreResolve())
		{
			return true;
		}
		for (const TSharedPtr<FSelectorHelper>& MappedHelper : Helpers)
		{
			if (MappedHelper && MappedHelper->AnyPickerWantsPreResolve())
			{
				return true;
			}
		}
		return false;
	}

	void FCollectionSource::BeginPreResolve(const int32 NumPoints) const
	{
		if (Helper)
		{
			Helper->BeginPreResolve(NumPoints);
		}
		for (const TSharedPtr<FSelectorHelper>& MappedHelper : Helpers)
		{
			if (MappedHelper)
			{
				MappedHelper->BeginPreResolve(NumPoints);
			}
		}
	}

	bool FCollectionSource::TryGetHelpers(int32 Index, FSelectorHelper*& OutHelper, FMicroSelectorHelper*& OutMicroHelper)
	{
		if (SingleSource)
		{
			OutHelper = Helper.Get();
			OutMicroHelper = MicroHelper.Get();
			return true;
		}

		const int32* Idx = Indices.Find(*(Keys->GetData() + Index));
		if (!Idx)
		{
			OutHelper = nullptr;
			OutMicroHelper = nullptr;
			return false;
		}

		OutHelper = Helpers[*Idx].Get();
		OutMicroHelper = MicroHelpers.IsValidIndex(*Idx) ? MicroHelpers[*Idx].Get() : nullptr;
		return true;
	}

	// Utility Functions

	FSocketHelper::FSocketHelper(const FPCGExSocketOutputDetails* InDetails, const int32 InNumPoints)
		: PCGExStaging::FSocketHelper(InDetails, InNumPoints)
	{
	}


	// Lock-free hot path: InfosKeys is populated once at init via RegisterCollection and
	// immutable during parallel Add(). A miss means a node forgot to register a collection
	// that can surface as a Host -- setup bug, not a runtime condition. checkSlow catches it
	// in debug builds; shipping builds treat it as a no-op to stay crash-free.
	void FSocketHelper::Add(const int32 Index, const uint64 EntryHash, const FPCGExAssetCollectionEntry* Entry)
	{
		const int32* IdxPtr = InfosKeys.Find(EntryHash);
		checkSlow(IdxPtr);
		if (!IdxPtr)
		{
			return;
		}

		FPlatformAtomics::InterlockedIncrement(&SocketInfosList[*IdxPtr].Count);
		Mapping[Index] = *IdxPtr;
	}

	// Walk every leaf entry reachable from InCollection (self + FlatHosts) and populate the
	// InfosKeys/SocketInfosList. Must run before any parallel Add(). Write lock is held so
	// multiple sequential RegisterCollection() calls from different top-level collections
	// compose correctly.
	void FSocketHelper::RegisterCollection(UPCGExAssetCollection* InCollection)
	{
		if (!InCollection)
		{
			return;
		}

		const TArray<TObjectPtr<UPCGExAssetCollection>>& FlatHosts = InCollection->GetFlatHosts();

		FWriteScopeLock WriteLock(SocketLock);

		for (const TObjectPtr<UPCGExAssetCollection>& HostPtr : FlatHosts)
		{
			UPCGExAssetCollection* Host = HostPtr.Get();
			if (!Host)
			{
				continue;
			}

			// GUID-keyed, matches both AssetStaging's Add() hash and LoadSockets' GetSimplifiedEntryHash.
			const uint32 HostGUID = Host->GetCollectionGUID();

			Host->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 /*Idx*/)
			{
				// Leaf-entry only: sub-collection entries don't carry Staging/Sockets themselves --
				// their socket data lives on the sub-collection's own leaf entries, which we
				// reach via the FlatHosts walk.
				if (!Entry || Entry->bIsSubCollection)
				{
					return;
				}

				const uint64 EntryHash = PCGEx::H64(HostGUID, Entry->Staging.InternalIndex);
				if (InfosKeys.Contains(EntryHash))
				{
					return;
				}

				int32 NewIdx = -1;
				PCGExStaging::FSocketInfos& NewInfos = NewSocketInfos(EntryHash, NewIdx);
				NewInfos.Path = Entry->Staging.Path;
				NewInfos.Category = Entry->Category;
				NewInfos.Sockets = Entry->Staging.Sockets;

				FilterSocketInfos(NewIdx);
			});
		}
	}
}

#pragma region ResolveLevelActor

namespace PCGExCollections
{
	AActor* ResolveLevelActor(const FSoftObjectPath& ActorPath, TSharedPtr<FStreamableHandle>& OutHandle, FString* OutFailure)
	{
		auto Fail = [OutFailure](FString&& Reason) -> AActor*
		{
			if (OutFailure)
			{
				*OutFailure = MoveTemp(Reason);
			}
			return nullptr;
		};

		if (!ActorPath.IsValid() || !ActorPath.IsSubobject())
		{
			return Fail(TEXT("not an actor path"));
		}

		AActor* Actor = Cast<AActor>(ActorPath.ResolveObject());

		if (!Actor)
		{
#if WITH_EDITOR
			// Asset-registry query, no load. Checked BEFORE loading: ULevel::PostLoad only pulls
			// external actors into a cold world for non-partitioned levels, so a partitioned one
			// would load fine and then contain nothing.
			if (ULevel::GetIsLevelPartitionedFromPackage(ActorPath.GetLongPackageFName()))
			{
				return Fail(FString::Printf(TEXT("level '%s' is World Partition -- only an open partitioned level can be read"), *ActorPath.GetLongPackageName()));
			}
#endif

			OutHandle = PCGExHelpers::LoadBlocking_AnyThread(ActorPath);
			Actor = Cast<AActor>(ActorPath.ResolveObject());
			if (!Actor)
			{
				PCGExHelpers::SafeReleaseHandle(OutHandle);
				return Fail(FString::Printf(TEXT("actor '%s' not found (level missing, actor deleted or renamed)"), *ActorPath.ToString()));
			}
		}

		// No-op on a live world; a cold one reads identity transforms until this runs.
		PCGExHelpers::EnsureWorldTransformsCurrent(Actor->GetWorld());
		return Actor;
	}
}

#pragma endregion
