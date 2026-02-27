// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionsHelpers.h"


#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

namespace PCGExCollections
{
	// Distribution Helper Implementation

	FDistributionHelper::FDistributionHelper(UPCGExAssetCollection* InCollection, const FPCGExAssetDistributionDetails& InDetails)
		: Collection(InCollection), Details(InDetails)
	{
	}

	// Loads the collection cache, initializes category getter (if category filtering is enabled),
	// and index getter (if distribution mode is Index). The index getter optionally tracks
	// min/max for remapping to collection size.
	bool FDistributionHelper::Init(const TSharedRef<PCGExData::FFacade>& InDataFacade)
	{
		Cache = Collection->LoadCache();

		if (Cache->IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, InDataFacade->GetContext(), FTEXT("FDistributionHelper got an empty Collection."));
			return false;
		}

		if (Details.bUseCategories)
		{
			CategoryGetter = Details.Category.GetValueSetting();
			if (!CategoryGetter->Init(InDataFacade)) { return false; }
		}

		if (Details.Distribution == EPCGExDistribution::Index)
		{
			const bool bWantsMinMax = Details.IndexSettings.bRemapIndexToCollectionSize;

			IndexGetter = Details.IndexSettings.GetValueSettingIndex();
			if (!IndexGetter->Init(InDataFacade, !bWantsMinMax, bWantsMinMax)) { return false; }

			MaxInputIndex = IndexGetter->Max();
		}

		return true;
	}

	// Entry picking: category filter → distribution strategy → subcollection recursion.
	// When a category is active, the distribution strategy is applied within the category.
	// If the category yields a subcollection entry, WorkingCollection switches to it and
	// the main distribution strategy is applied to the subcollection instead.
	FPCGExEntryAccessResult FDistributionHelper::GetEntry(int32 PointIndex, int32 Seed) const
	{
		const UPCGExAssetCollection* WorkingCollection = Collection;

		if (CategoryGetter)
		{
			const FName CategoryKey = CategoryGetter->Read(PointIndex);
			TSharedPtr<PCGExAssetCollection::FCategory>* CategoryPtr = Cache->Categories.Find(CategoryKey);

			if (!CategoryPtr || !CategoryPtr->IsValid() || (*CategoryPtr)->IsEmpty())
			{
				return FPCGExEntryAccessResult{};
			}

			const TSharedPtr<PCGExAssetCollection::FCategory>& Category = *CategoryPtr;

			int32 PickIndex;

			if (Category->Num() == 1)
			{
				PickIndex = Category->Indices[0];
			}
			else
			{
				switch (Details.Distribution)
				{
				default:
				case EPCGExDistribution::WeightedRandom:
					PickIndex = Category->GetPickRandomWeighted(Seed);
					break;
				case EPCGExDistribution::Random:
					PickIndex = Category->GetPickRandom(Seed);
					break;
				case EPCGExDistribution::Index:
					{
						const auto& IndexSettings = Details.IndexSettings;
						const int32 CategoryMaxIndex = Category->Num() - 1;
						double UserIndex = IndexGetter->Read(PointIndex);

						if (IndexSettings.bRemapIndexToCollectionSize && MaxInputIndex > 0)
						{
							UserIndex = PCGExMath::Remap(UserIndex, 0, MaxInputIndex, 0, CategoryMaxIndex);
							UserIndex = PCGExMath::TruncateDbl(UserIndex, IndexSettings.TruncateRemap);
						}

						const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(UserIndex), CategoryMaxIndex, IndexSettings.IndexSafety);
						PickIndex = Category->GetPick(Sanitized, IndexSettings.PickMode);
					}
					break;
				}
			}

			auto Result = Collection->GetEntryRaw(PickIndex);
			if (Result && Result.Entry->HasValidSubCollection())
			{
				WorkingCollection = Result.Entry->GetSubCollectionPtr();
			}
			else
			{
				return Result;
			}
		}

		// Apply distribution strategy (no category, or category resolved to a subcollection)
		switch (Details.Distribution)
		{
		case EPCGExDistribution::WeightedRandom: return WorkingCollection->GetEntryWeightedRandom(Seed);

		case EPCGExDistribution::Random: return WorkingCollection->GetEntryRandom(Seed);

		case EPCGExDistribution::Index:
			{
				const auto& IndexSettings = Details.IndexSettings;
				const int32 MaxIndex = const_cast<UPCGExAssetCollection*>(WorkingCollection)->LoadCache()->Main->Num() - 1;
				double PickedIndex = IndexGetter->Read(PointIndex);

				if (IndexSettings.bRemapIndexToCollectionSize && MaxInputIndex > 0)
				{
					PickedIndex = PCGExMath::Remap(PickedIndex, 0, MaxInputIndex, 0, MaxIndex);
					PickedIndex = PCGExMath::TruncateDbl(PickedIndex, IndexSettings.TruncateRemap);
				}

				const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(PickedIndex), MaxIndex, IndexSettings.IndexSafety);

				return WorkingCollection->GetEntry(Sanitized, Seed, IndexSettings.PickMode);
			}
		}

		return FPCGExEntryAccessResult{};
	}

	FPCGExEntryAccessResult FDistributionHelper::GetEntry(int32 PointIndex, int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const
	{
		if (TagInheritance == 0)
		{
			return GetEntry(PointIndex, Seed);
		}

		const UPCGExAssetCollection* WorkingCollection = Collection;

		// Handle category-based selection
		if (CategoryGetter)
		{
			const FName CategoryKey = CategoryGetter->Read(PointIndex);
			TSharedPtr<PCGExAssetCollection::FCategory>* CategoryPtr = Cache->Categories.Find(CategoryKey);

			if (!CategoryPtr || !CategoryPtr->IsValid() || (*CategoryPtr)->IsEmpty())
			{
				return FPCGExEntryAccessResult{};
			}

			const TSharedPtr<PCGExAssetCollection::FCategory>& Category = *CategoryPtr;

			int32 PickIndex;

			if (Category->Num() == 1)
			{
				PickIndex = Category->Indices[0];
			}
			else
			{
				switch (Details.Distribution)
				{
				default:
				case EPCGExDistribution::WeightedRandom:
					PickIndex = Category->GetPickRandomWeighted(Seed);
					break;
				case EPCGExDistribution::Random:
					PickIndex = Category->GetPickRandom(Seed);
					break;
				case EPCGExDistribution::Index:
					{
						const auto& IndexSettings = Details.IndexSettings;
						const int32 CategoryMaxIndex = Category->Num() - 1;
						double UserIndex = IndexGetter->Read(PointIndex);

						if (IndexSettings.bRemapIndexToCollectionSize && MaxInputIndex > 0)
						{
							UserIndex = PCGExMath::Remap(UserIndex, 0, MaxInputIndex, 0, CategoryMaxIndex);
							UserIndex = PCGExMath::TruncateDbl(UserIndex, IndexSettings.TruncateRemap);
						}

						const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(UserIndex), CategoryMaxIndex, IndexSettings.IndexSafety);
						PickIndex = Category->GetPick(Sanitized, IndexSettings.PickMode);
					}
					break;
				}
			}

			auto Result = Collection->GetEntryRaw(PickIndex, TagInheritance, OutTags);
			if (Result && Result.Entry->HasValidSubCollection())
			{
				WorkingCollection = Result.Entry->GetSubCollectionPtr();
			}
			else
			{
				return Result;
			}
		}

		// Apply distribution strategy with tags
		switch (Details.Distribution)
		{
		case EPCGExDistribution::WeightedRandom: return WorkingCollection->GetEntryWeightedRandom(Seed, TagInheritance, OutTags);

		case EPCGExDistribution::Random: return WorkingCollection->GetEntryRandom(Seed, TagInheritance, OutTags);

		case EPCGExDistribution::Index:
			{
				const auto& IndexSettings = Details.IndexSettings;
				const int32 MaxIndex = const_cast<UPCGExAssetCollection*>(WorkingCollection)->LoadCache()->Main->Num() - 1;
				double PickedIndex = IndexGetter->Read(PointIndex);

				if (IndexSettings.bRemapIndexToCollectionSize && MaxInputIndex > 0)
				{
					PickedIndex = PCGExMath::Remap(PickedIndex, 0, MaxInputIndex, 0, MaxIndex);
					PickedIndex = PCGExMath::TruncateDbl(PickedIndex, IndexSettings.TruncateRemap);
				}

				const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(PickedIndex), MaxIndex, IndexSettings.IndexSafety);

				return WorkingCollection->GetEntry(Sanitized, Seed, IndexSettings.PickMode, TagInheritance, OutTags);
			}
		}

		return FPCGExEntryAccessResult{};
	}

	// MicroDistribution Helper Implementation

	FMicroDistributionHelper::FMicroDistributionHelper(const FPCGExMicroCacheDistributionDetails& InDetails)
		: Details(InDetails)
	{
	}

	bool FMicroDistributionHelper::Init(const TSharedRef<PCGExData::FFacade>& InDataFacade)
	{
		if (Details.Distribution == EPCGExDistribution::Index)
		{
			IndexGetter = Details.IndexSettings.GetValueSettingIndex();
			if (!IndexGetter->Init(InDataFacade, true, false)) { return false; }
			MaxInputIndex = IndexGetter->Max();
		}

		return true;
	}

	int32 FMicroDistributionHelper::GetPick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const
	{
		if (!InMicroCache || InMicroCache->IsEmpty()) { return -1; }

		switch (Details.Distribution)
		{
		case EPCGExDistribution::WeightedRandom: return InMicroCache->GetPickRandomWeighted(Seed);

		case EPCGExDistribution::Random: return InMicroCache->GetPickRandom(Seed);

		case EPCGExDistribution::Index:
			{
				const int32 Index = IndexGetter ? static_cast<int32>(IndexGetter->Read(PointIndex)) : 0;
				const int32 Sanitized = PCGExMath::SanitizeIndex(Index, InMicroCache->Num() - 1, Details.IndexSettings.IndexSafety);
				return InMicroCache->GetPick(Sanitized, Details.IndexSettings.PickMode);
			}
		}

		return -1;
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

	uint64 FPickPacker::GetPickIdx(const UPCGExAssetCollection* InCollection, int16 InIndex, int16 InSecondaryIndex)
	{
		const uint32 ItemHash = PCGEx::H32(InIndex, InSecondaryIndex + 1);
		const uint32 GUID = InCollection->GetCollectionGUID();

		{
			FReadScopeLock ReadScopeLock(AssetCollectionsLock);
			if (CollectionMap.Contains(InCollection))
			{
				return PCGEx::H64(GUID, ItemHash);
			}
		}

		{
			FWriteScopeLock WriteScopeLock(AssetCollectionsLock);
			if (!CollectionMap.Contains(InCollection))
			{
				CollectionMap.Add(InCollection, GUID);
			}
			return PCGEx::H64(GUID, ItemHash);
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

	FPickUnpacker::~FPickUnpacker()
	{
		PCGExHelpers::SafeReleaseHandle(CollectionsHandle);
	}

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
			CollectionsHandle = PCGExHelpers::LoadBlocking_AnyThread(CollectionPaths);
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
				if (CollectionMap[Idx] == Collection) { continue; }

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

			if (!ParamData) { continue; }

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
	bool FPickUnpacker::BuildPartitions(const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& InstanceLists)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPickUnpacker::BuildPartitions);

		FPCGAttributePropertyInputSelector HashSelector;
		HashSelector.Update(Labels::Tag_EntryIdx.ToString());

		TUniquePtr<const IPCGAttributeAccessor> HashAttributeAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, HashSelector);
		TUniquePtr<const IPCGAttributeAccessorKeys> HashKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, HashSelector);

		if (!HashAttributeAccessor || !HashKeys) { return false; }

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
			if (const int32* Index = IndexedPartitions.Find(EntryHash); !Index)
			{
				FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
				NewInstanceList.AttributePartitionIndex = EntryHash;
				NewInstanceList.PointData = InPointData;
				NewInstanceList.InstancesIndices.Reserve(SafeReserve);
				NewInstanceList.InstancesIndices.Emplace(i);

				IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
			}
			else
			{
				InstanceLists[*Index].InstancesIndices.Emplace(i);
			}
		}

		return !IndexedPartitions.IsEmpty();
	}

	void FPickUnpacker::InsertEntry(const uint64 EntryHash, const int32 EntryIndex, TArray<FPCGMeshInstanceList>& InstanceLists)
	{
		if (const int32* Index = IndexedPartitions.Find(EntryHash); !Index)
		{
			FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
			NewInstanceList.AttributePartitionIndex = EntryHash;
			NewInstanceList.PointData = PointData;
			NewInstanceList.InstancesIndices.Reserve(PointData->GetNumPoints() / (NumUniqueEntries * 2));
			NewInstanceList.InstancesIndices.Emplace(EntryIndex);

			IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
		}
		else
		{
			InstanceLists[*Index].InstancesIndices.Emplace(EntryIndex);
		}
	}

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
		if (!Collection || !(*Collection)->IsValidIndex(EntryIndex)) { return nullptr; }

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
		if (!Collection) { return FPCGExEntryAccessResult{}; }

		return Collection->GetEntryRaw(EntryIndex);
	}

	// Collection Source Implementation

	FCollectionSource::FCollectionSource(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
		: DataFacade(InDataFacade)
	{
	}

	bool FCollectionSource::Init(UPCGExAssetCollection* InCollection)
	{
		SingleSource = InCollection;
		Helper = MakeShared<FDistributionHelper>(InCollection, DistributionSettings);
		if (!Helper->Init(DataFacade.ToSharedRef())) { return false; }

		// Create micro helper for mesh collections
		if (InCollection->IsType(PCGExAssetCollection::TypeIds::Mesh))
		{
			MicroHelper = MakeShared<FMicroDistributionHelper>(EntryDistributionSettings);
			if (!MicroHelper->Init(DataFacade.ToSharedRef())) { return false; }
		}

		return true;
	}

	bool FCollectionSource::Init(const TMap<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& InMap, const TSharedPtr<TArray<PCGExValueHash>>& InKeys)
	{
		Keys = InKeys;
		if (!Keys) { return false; }

		const int32 NumElements = InMap.Num();
		Helpers.Reserve(NumElements);
		MicroHelpers.Reserve(NumElements);

		for (const TPair<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& Pair : InMap)
		{
			UPCGExAssetCollection* Collection = Pair.Value.Get();

			TSharedPtr<FDistributionHelper> NewHelper = MakeShared<FDistributionHelper>(Collection, DistributionSettings);
			if (!NewHelper->Init(DataFacade.ToSharedRef())) { continue; }

			Indices.Add(Pair.Key, Helpers.Add(NewHelper));

			// Create micro helper for mesh collections
			if (Collection->IsType(PCGExAssetCollection::TypeIds::Mesh))
			{
				TSharedPtr<FMicroDistributionHelper> NewMicroHelper = MakeShared<FMicroDistributionHelper>(EntryDistributionSettings);
				if (!NewMicroHelper->Init(DataFacade.ToSharedRef()))
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

	bool FCollectionSource::TryGetHelpers(int32 Index, FDistributionHelper*& OutHelper, FMicroDistributionHelper*& OutMicroHelper)
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


	void FSocketHelper::Add(const int32 Index, const uint64 EntryHash, const FPCGExAssetCollectionEntry* Entry)
	{
		const int32* IdxPtr = nullptr;

		{
			FReadScopeLock ReadLock(SocketLock);
			IdxPtr = InfosKeys.Find(EntryHash);
		}

		int32 Idx = -1;

		if (!IdxPtr)
		{
			FWriteScopeLock WriteLock(SocketLock);

			IdxPtr = InfosKeys.Find(EntryHash);
			if (IdxPtr)
			{
				Idx = *IdxPtr;
			}
			else
			{
				PCGExStaging::FSocketInfos& NewInfos = NewSocketInfos(EntryHash, Idx);

				NewInfos.Path = Entry->Staging.Path;
				NewInfos.Category = Entry->Category;
				NewInfos.Sockets = Entry->Staging.Sockets;

				FilterSocketInfos(Idx);
			}
		}
		else
		{
			Idx = *IdxPtr;
		}

		FPlatformAtomics::InterlockedIncrement(&SocketInfosList[Idx].Count);
		Mapping[Index] = Idx;
	}
}
