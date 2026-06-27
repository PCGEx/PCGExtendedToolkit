// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionsHelpers.h"

#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/Level.h"
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
	// Synthesize a transient Classic factory that mirrors the Legacy Details struct. Used when
	// the caller did not provide an ExternalFactory. Keeps the post-Init code path unified:
	// everything downstream reads from ActiveFactory->BaseConfig regardless of mode.
	UPCGExSelectorFactoryData* BuildLegacyFactory(FPCGExContext* InContext, const FPCGExAssetDistributionDetails& InDetails)
	{
		UPCGExSelectorClassicFactoryData* Factory = InContext->ManagedObjects->New<UPCGExSelectorClassicFactoryData>();

		Factory->Config.Mode = InDetails.Distribution;
		Factory->Config.IndexConfig = InDetails.IndexSettings;
		Factory->BaseConfig.SeedComponents = InDetails.SeedComponents;
		Factory->BaseConfig.LocalSeed = InDetails.LocalSeed;
		Factory->BaseConfig.bUseCategories = InDetails.bUseCategories;
		Factory->BaseConfig.Category = InDetails.Category;
		Factory->BaseConfig.MissingCategoryBehavior = InDetails.MissingCategoryBehavior;

		// BaseConfig.EntryDistribution stays default -- the Legacy EntryDistributionSettings
		// lives on the consuming node and is plumbed through FMicroSelectorHelper separately.
		return Factory;
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

		Cache = Collection->LoadCache();

		if (Cache->IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, InDataFacade->GetContext(), FTEXT("FSelectorHelper got an empty Collection."));
			return false;
		}

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
		}

		// Sync the inline Details struct with the effective factory config so callers can
		// read Helper->Details.SeedComponents / LocalSeed without knowing which mode is active.
		Details.SeedComponents = BaseConfig.SeedComponents;
		Details.LocalSeed = BaseConfig.LocalSeed;

		return true;
	}

	// Resolve which picker to use for this point. When categories are enabled and the point's
	// Category attribute doesn't match any named category, MissingCategoryBehavior controls
	// whether we skip (return nullptr) or fall back to MainPickerOp.
	// Returns nullptr when the result should be an empty FPCGExEntryAccessResult.
	const FPCGExEntryPickerOperation* FSelectorHelper::ResolvePickerForPoint(int32 PointIndex) const
	{
		if (!CategoryGetter)
		{
			return MainPickerOp.Get();
		}

		const FName CategoryKey = CategoryGetter->Read(PointIndex);
		if (const int32* IdxPtr = Cache->CategoryNameToIndex.Find(CategoryKey))
		{
			if (CategoryPickerOpsByIndex.IsValidIndex(*IdxPtr))
			{
				if (const TSharedPtr<FPCGExEntryPickerOperation>& Op = CategoryPickerOpsByIndex[*IdxPtr])
				{
					return Op.Get();
				}
			}
		}

		return ActiveFactory->BaseConfig.MissingCategoryBehavior == EPCGExMissingCategoryBehavior::UseMain
			? MainPickerOp.Get()
			: nullptr;
	}

	// Entry picking: resolve the active picker (category-aware) -> pick a raw entries index
	// -> resolve entry -> handle subcollection recursion via the "fallback to WeightedRandom"
	// policy (matches current behavior when the picked entry is a subcollection).
	FPCGExEntryAccessResult FSelectorHelper::GetEntry(int32 PointIndex, int32 Seed, const bool bFlattenSubCollections) const
	{
		const FPCGExEntryPickerOperation* Op = ResolvePickerForPoint(PointIndex);
		if (!Op)
		{
			return FPCGExEntryAccessResult{};
		}

		const int32 Raw = Op->Pick(PointIndex, Seed);
		FPCGExEntryAccessResult Result = Collection->GetEntryRaw(Raw);
		if (Result && (!bFlattenSubCollections && Result.Entry->HasValidSubCollection()))
		{
			return Result.Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed);
		}
		return Result;
	}

	FPCGExEntryAccessResult FSelectorHelper::GetEntry(int32 PointIndex, int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags, const bool bFlattenSubCollections) const
	{
		if (TagInheritance == 0)
		{
			return GetEntry(PointIndex, Seed);
		}

		const FPCGExEntryPickerOperation* Op = ResolvePickerForPoint(PointIndex);
		if (!Op)
		{
			return FPCGExEntryAccessResult{};
		}

		const int32 Raw = Op->Pick(PointIndex, Seed);
		FPCGExEntryAccessResult Result = Collection->GetEntryRaw(Raw, TagInheritance, OutTags);
		if (Result && (!bFlattenSubCollections && Result.Entry->HasValidSubCollection()))
		{
			return Result.Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed, TagInheritance, OutTags);
		}
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
		FPCGMetadataAttribute<int32>* CollectionIdx = InAttributeSet->Metadata->FindOrCreateAttribute<int32>(
			Labels::Tag_CollectionIdx, 0,
			false, true, true);
		FPCGMetadataAttribute<FSoftObjectPath>* CollectionPath = InAttributeSet->Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			Labels::Tag_CollectionPath, FSoftObjectPath(),
			false, true, true);

		for (const TPair<const UPCGExAssetCollection*, uint32>& Pair : CollectionMap)
		{
			const int64 Key = InAttributeSet->Metadata->AddEntry();
			CollectionIdx->SetValue(Key, Pair.Value);
			CollectionPath->SetValue(Key, FSoftObjectPath(Pair.Key));
		}
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
		HashSelector.Update(Labels::Tag_EntryIdx.ToString());

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
	void FPickUnpacker::InsertEntry(const uint64 EntryHash, const int32 EntryIndex, TArray<T>& InstanceLists)
	{
		using FTraits = TInstanceListTraits<T>;

		if (const int32* Index = IndexedPartitions.Find(EntryHash);
			!Index)
		{
			T& NewInstanceList = InstanceLists.Emplace_GetRef();
			NewInstanceList.AttributePartitionIndex = EntryHash;
			FTraits::SetPointData(NewInstanceList, PointData);
			TArray<int32>& Indices = FTraits::GetIndices(NewInstanceList);
			Indices.Reserve(PointData->GetNumPoints() / (NumUniqueEntries * 2));
			Indices.Emplace(EntryIndex);

			IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
		}
		else
		{
			FTraits::GetIndices(InstanceLists[*Index]).Emplace(EntryIndex);
		}
	}

	// Explicit instantiations. Add a new pair here when introducing a new instance-list type
	// alongside its TInstanceListTraits specialization above.
	template bool FPickUnpacker::BuildPartitions<FPCGMeshInstanceList>(const UPCGBasePointData*, TArray<FPCGMeshInstanceList>&);
	template bool FPickUnpacker::BuildPartitions<FPCGSkinnedMeshInstanceList>(const UPCGBasePointData*, TArray<FPCGSkinnedMeshInstanceList>&);
	template void FPickUnpacker::InsertEntry<FPCGMeshInstanceList>(const uint64, const int32, TArray<FPCGMeshInstanceList>&);
	template void FPickUnpacker::InsertEntry<FPCGSkinnedMeshInstanceList>(const uint64, const int32, TArray<FPCGSkinnedMeshInstanceList>&);

	UPCGExAssetCollection* FPickUnpacker::UnpackHash(uint64 EntryHash, int16& OutPrimaryIndex, int16& OutSecondaryIndex)
	{
		uint32 CollectionIdx = 0;
		uint32 OutEntryIndices = 0;

		PCGEx::H64(EntryHash, CollectionIdx, OutEntryIndices);

		uint16 EntryIndex = 0;
		uint16 SecondaryIndex = 0;

		PCGEx::H32(OutEntryIndices, EntryIndex, SecondaryIndex);
		OutSecondaryIndex = SecondaryIndex - 1; // minus one because we do +1 during packing

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
