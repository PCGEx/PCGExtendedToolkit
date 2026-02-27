// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExSocketHelpers.h"

namespace PCGExData
{
	class FPointIOCollection;
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

struct FPCGContext;
struct FPCGMeshInstanceList;
class UPCGBasePointData;
class UPCGParamData;

/**
 * Runtime helpers for consuming collections in PCG nodes.
 *
 * Two-phase pipeline:
 *   Phase 1 - Generation (AssetStaging, CollectionToModuleInfos):
 *     FCollectionSource → wraps FDistributionHelper + FMicroDistributionHelper
 *     FPickPacker       → serializes picks to attribute set ("Collection Map")
 *
 *   Phase 2 - Consumption (LoadPCGData, LoadProperties, LoadSockets, Fitting, TypeFilter):
 *     FPickUnpacker     → deserializes Collection Map, resolves picks back to entries
 *
 * Typical generation flow (see PCGExAssetStaging.cpp):
 *   1. Create FCollectionSource with your data facade
 *   2. Set DistributionSettings + EntryDistributionSettings, call Init(Collection)
 *   3. In ProcessPoints: TryGetHelpers() → Helper->GetEntry() → MicroHelper->GetPick()
 *   4. Write entry hash via FPickPacker::GetPickIdx() to an int64 attribute
 *   5. After processing: FPickPacker::PackToDataset() serializes the mapping
 *
 * Typical consumption flow (see PCGExStagingLoadPCGData.cpp):
 *   1. Create FPickUnpacker, call UnpackPin() to load the Collection Map
 *   2. In ProcessPoints: read int64 hash → UnpackHash() or ResolveEntry() → get entry + secondary index
 *   3. Use entry data (staging path, bounds, sockets, etc.)
 *
 * Hash encoding (FPickPacker/FPickUnpacker):
 *   uint64 = H64( CollectionGUID, H32(EntryIndex, SecondaryIndex+1) )
 *   CollectionGUID is a persistent uint32 on each UPCGExAssetCollection.
 *   This packs collection identity + entry + variant into a single attribute value.
 */
namespace PCGExCollections
{
	/**
	 * Per-point entry picker. Reads distribution settings (index/random/weighted) and
	 * optional category filtering, then picks entries from a collection's cache.
	 *
	 * Usage:
	 *   auto Helper = MakeShared<FDistributionHelper>(Collection, DistributionDetails);
	 *   Helper->Init(DataFacade);
	 *   // In parallel loop:
	 *   FPCGExEntryAccessResult Result = Helper->GetEntry(PointIndex, Seed);
	 *
	 * Category support: when bUseCategories is enabled, picks are restricted to the named
	 * sub-category within the cache. If the picked entry is a subcollection, recursion
	 * continues into it via GetEntryWeightedRandom.
	 */
	class PCGEXCOLLECTIONS_API FDistributionHelper : public TSharedFromThis<FDistributionHelper>
	{
	protected:
		PCGExAssetCollection::FCache* Cache = nullptr;
		UPCGExAssetCollection* Collection = nullptr;

		TSharedPtr<PCGExDetails::TSettingValue<double>> IndexGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FName>> CategoryGetter;
		double MaxInputIndex = 0;

	public:
		FPCGExAssetDistributionDetails Details;

		explicit FDistributionHelper(UPCGExAssetCollection* InCollection, const FPCGExAssetDistributionDetails& InDetails);

		/**
		 * Initialize the helper with a data facade
		 * @return true if initialization successful
		 */
		bool Init(const TSharedRef<PCGExData::FFacade>& InDataFacade);

		/**
		 * Get an entry for a specific point
		 * @param PointIndex Index of the point
		 * @param Seed Random seed for this point
		 * @return Access result containing entry and host collection
		 */
		FPCGExEntryAccessResult GetEntry(int32 PointIndex, int32 Seed) const;

		/**
		 * Get an entry with tag inheritance
		 * @param PointIndex Index of the point
		 * @param Seed Random seed for this point
		 * @param TagInheritance Bitmask of EPCGExAssetTagInheritance flags
		 * @param OutTags Set to append inherited tags to
		 * @return Access result containing entry and host collection
		 */
		FPCGExEntryAccessResult GetEntry(int32 PointIndex, int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const;

		/** Get the underlying collection */
		UPCGExAssetCollection* GetCollection() const { return Collection; }

		/** Get the collection's type ID */
		PCGExAssetCollection::FTypeId GetCollectionTypeId() const
		{
			return Collection ? Collection->GetTypeId() : PCGExAssetCollection::TypeIds::None;
		}
	};

	/**
	 * Per-point sub-entry picker operating on an entry's FMicroCache.
	 * Selects a variant index (e.g. material override) using the same distribution
	 * modes as the main helper (index/random/weighted). The picked index is then
	 * used as a "secondary index" in the packing scheme.
	 *
	 * Usage:
	 *   auto MicroHelper = MakeShared<FMicroDistributionHelper>(MicroDistDetails);
	 *   MicroHelper->Init(DataFacade);
	 *   // In parallel loop:
	 *   int32 Pick = MicroHelper->GetPick(Entry->MicroCache.Get(), PointIndex, Seed);
	 *   // Pick is then passed to ApplyMaterials() or packed as SecondaryIndex
	 */
	class PCGEXCOLLECTIONS_API FMicroDistributionHelper : public TSharedFromThis<FMicroDistributionHelper>
	{
	protected:
		TSharedPtr<PCGExDetails::TSettingValue<double>> IndexGetter;
		double MaxInputIndex = 0;

	public:
		FPCGExMicroCacheDistributionDetails Details;

		explicit FMicroDistributionHelper(const FPCGExMicroCacheDistributionDetails& InDetails);

		bool Init(const TSharedRef<PCGExData::FFacade>& InDataFacade);

		/**
		 * Get a pick index from a MicroCache
		 * @param InMicroCache The MicroCache to pick from
		 * @param PointIndex Index of the point
		 * @param Seed Random seed
		 * @return The picked index, or -1 if invalid
		 */
		int32 GetPick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const;
	};

	/**
	 * Serializes collection references and per-point entry picks into a UPCGParamData
	 * attribute set (the "Collection Map"). This is the bridge between generation nodes
	 * (AssetStaging) and consumption nodes (LoadPCGData, LoadSockets, Fitting, etc.).
	 *
	 * IMPORTANT: InIndex is a RAW Entries array index (Staging.InternalIndex), NOT a
	 * cache-adjusted index. The unpacker resolves these via GetEntryRaw(), not GetEntryAt().
	 *
	 * Thread-safe: GetPickIdx() can be called from parallel ProcessPoints loops.
	 * The attribute set contains two attributes per collection:
	 *   - Tag_CollectionIdx (int32): packed collection identifier
	 *   - Tag_CollectionPath (FSoftObjectPath): collection asset path for loading
	 *
	 * Usage:
	 *   // In Boot:
	 *   Packer = MakeShared<FPickPacker>();  // or FPickPacker(Context) for backward compat
	 *   // In ProcessPoints (parallel):
	 *   uint64 Hash = Packer->GetPickIdx(EntryHost, Staging.InternalIndex, SecondaryIndex);
	 *   HashWriter->SetValue(Index, Hash);
	 *   // After processing:
	 *   UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
	 *   Packer->PackToDataset(OutputSet);
	 *   // Output to "Map" pin
	 */
	class PCGEXCOLLECTIONS_API FPickPacker : public TSharedFromThis<FPickPacker>
	{
		TMap<const UPCGExAssetCollection*, uint32> CollectionMap;
		mutable FRWLock AssetCollectionsLock;

	public:
		FPickPacker() = default;
		explicit FPickPacker(FPCGContext* InContext);

		/**
		 * Get a packed index for a collection entry pick.
		 * IMPORTANT: InIndex must be a RAW Entries array index (e.g. Staging.InternalIndex),
		 * not a cache-adjusted index. The unpacker uses GetEntryRaw() to resolve it.
		 * @param InCollection The collection
		 * @param InIndex Raw Entries array index (Staging.InternalIndex)
		 * @param InSecondaryIndex Secondary index (e.g., material variant)
		 * @return Packed 64-bit identifier
		 */
		uint64 GetPickIdx(const UPCGExAssetCollection* InCollection, int16 InIndex, int16 InSecondaryIndex);

		/** Write collection mapping to an attribute set */
		void PackToDataset(const UPCGParamData* InAttributeSet);
	};

	/**
	 * Deserializes a Collection Map (produced by FPickPacker) back into usable collection
	 * references. Loads the referenced collections, then resolves per-point hashes into
	 * concrete entries + secondary indices.
	 *
	 * IMPORTANT: Packed hashes contain RAW Entries array indices (not cache-adjusted).
	 * Resolution uses GetEntryRaw(), not GetEntryAt(). This distinction matters when
	 * entries with Weight=0 are excluded from the cache -- raw indices remain stable
	 * while cache indices shift.
	 *
	 * Used by all consumption nodes: LoadPCGData, LoadProperties, LoadSockets,
	 * Fitting, TypeFilter.
	 *
	 * Usage:
	 *   // In Boot:
	 *   Unpacker = MakeShared<FPickUnpacker>();
	 *   Unpacker->UnpackPin(Context);  // reads from "Map" input pin
	 *   if (!Unpacker->HasValidMapping()) { return false; }
	 *   // In ProcessPoints:
	 *   int64 Hash = HashGetter->Read(Index);
	 *   int16 SecondaryIndex;
	 *   FPCGExEntryAccessResult Result = Unpacker->ResolveEntry(Hash, SecondaryIndex);
	 *   // Use Result.Entry->Staging, Result.Host, SecondaryIndex
	 */
	class PCGEXCOLLECTIONS_API FPickUnpacker : public TSharedFromThis<FPickUnpacker>
	{
	protected:
		TMap<uint32, UPCGExAssetCollection*> CollectionMap;
		TSharedPtr<struct FStreamableHandle> CollectionsHandle;
		int32 NumUniqueEntries = 0;
		const UPCGBasePointData* PointData = nullptr;

	public:
		TMap<int64, TSharedPtr<TArray<int32>>> HashedPartitions;
		TMap<int64, int32> IndexedPartitions;

		FPickUnpacker() = default;
		~FPickUnpacker();

		bool HasValidMapping() const { return !CollectionMap.IsEmpty(); }

		/** Get read-only access to the collection map */
		const TMap<uint32, UPCGExAssetCollection*>& GetCollections() const { return CollectionMap; }

		/** Unpack collection mappings from an attribute set */
		bool UnpackDataset(FPCGContext* InContext, const UPCGParamData* InAttributeSet);

		/** Unpack from a specific input pin */
		void UnpackPin(FPCGContext* InContext, FName InPinLabel = NAME_None);

		/** Build point partitions from point data */
		bool BuildPartitions(const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& InstanceLists);

		void InsertEntry(const uint64 EntryHash, const int32 EntryIndex, TArray<FPCGMeshInstanceList>& InstanceLists);

		/**
		 * Resolve a packed hash to an entry
		 * @param EntryHash The packed hash
		 * @param OutPrimaryIndex Output: primary entry index
		 * @param OutSecondaryIndex Output: secondary index
		 * @return The collection, or nullptr if not found
		 */
		UPCGExAssetCollection* UnpackHash(uint64 EntryHash, int16& OutPrimaryIndex, int16& OutSecondaryIndex);

		/**
		 * Resolve an entry from a packed hash
		 * @param EntryHash The packed hash
		 * @param OutSecondaryIndex Output: secondary index
		 * @return Entry access result
		 */
		FPCGExEntryAccessResult ResolveEntry(uint64 EntryHash, int16& OutSecondaryIndex);
	};

	/**
	 * Unified facade for single or per-point collection sources. Wraps one or many
	 * FDistributionHelper + FMicroDistributionHelper pairs and routes TryGetHelpers()
	 * to the correct one based on point index.
	 *
	 * Two modes:
	 * - Single source: Init(Collection) -- all points share one collection
	 * - Mapped source: Init(Map, Keys) -- each point has a hash key that maps to
	 *   a different collection (loaded via TAssetLoader from per-point path attributes)
	 *
	 * MicroHelper is automatically created for mesh collections (material variant picking).
	 *
	 * Usage (see PCGExAssetStaging::FProcessor::Process):
	 *   Source = MakeShared<FCollectionSource>(PointDataFacade);
	 *   Source->DistributionSettings = Settings->DistributionSettings;
	 *   Source->EntryDistributionSettings = Settings->EntryDistributionSettings;
	 *   Source->Init(Collection);
	 *   // In ProcessPoints:
	 *   FDistributionHelper* Helper; FMicroDistributionHelper* MicroHelper;
	 *   if (Source->TryGetHelpers(Index, Helper, MicroHelper)) { ... }
	 */
	class PCGEXCOLLECTIONS_API FCollectionSource : public TSharedFromThis<FCollectionSource>
	{
		TSharedPtr<FDistributionHelper> Helper;
		TSharedPtr<FMicroDistributionHelper> MicroHelper;

		// For mapped sources
		TArray<TSharedPtr<FDistributionHelper>> Helpers;
		TArray<TSharedPtr<FMicroDistributionHelper>> MicroHelpers;
		TMap<PCGExValueHash, int32> Indices;

		TSharedPtr<TArray<PCGExValueHash>> Keys;
		TSharedPtr<PCGExData::FFacade> DataFacade;
		UPCGExAssetCollection* SingleSource = nullptr;

	public:
		FPCGExAssetDistributionDetails DistributionSettings;
		FPCGExMicroCacheDistributionDetails EntryDistributionSettings;

		explicit FCollectionSource(const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		/** Initialize with a single collection */
		bool Init(UPCGExAssetCollection* InCollection);

		/** Initialize with a mapped collection source */
		bool Init(const TMap<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& InMap, const TSharedPtr<TArray<PCGExValueHash>>& InKeys);

		/**
		 * Get helpers for a specific point index
		 * @param Index Point index
		 * @param OutHelper Output: distribution helper
		 * @param OutMicroHelper Output: micro distribution helper (may be null)
		 * @return true if valid helpers found
		 */
		bool TryGetHelpers(int32 Index, FDistributionHelper*& OutHelper, FMicroDistributionHelper*& OutMicroHelper);

		/** Check if this is a single source */
		bool IsSingleSource() const { return SingleSource != nullptr; }

		/** Get the single source collection (if applicable) */
		UPCGExAssetCollection* GetSingleSource() const { return SingleSource; }
	};

	/**
	 * Collection-aware socket helper. Extracts socket transforms from collection entries'
	 * staging data and builds per-entry socket point sets. Thread-safe Add() deduplicates
	 * by entry hash and accumulates socket info with atomic reference counting.
	 * Call Compile() after processing to output socket points to a FPointIOCollection.
	 */
	class PCGEXCOLLECTIONS_API FSocketHelper : public PCGExStaging::FSocketHelper
	{
	public:
		explicit FSocketHelper(const FPCGExSocketOutputDetails* InDetails, const int32 InNumPoints);

		void Add(const int32 Index, const uint64 EntryHash, const FPCGExAssetCollectionEntry* Entry);
	};
}
