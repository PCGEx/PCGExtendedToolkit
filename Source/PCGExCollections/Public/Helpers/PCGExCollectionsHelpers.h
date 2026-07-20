// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExH.h"
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
struct FPCGSkinnedMeshInstanceList;
class UPCGBasePointData;
class UPCGExSelectorFactoryData;
class UPCGManagedActors;
class UPCGParamData;
class FPCGExEntryPickerOperation;
class FPCGExMicroEntryPickerOperation;
class FPCGExPickerScratchBase;

namespace PCGExCollections
{
	class FSelectorSharedDataCache;
}

namespace PCGExMeshCollection
{
	class FMicroCache;
}

/**
 * Runtime helpers for consuming collections in PCG nodes.
 *
 * Two-phase pipeline:
 *   Phase 1 - Generation (AssetStaging, CollectionToModuleInfos):
 *     FCollectionSource → wraps FSelectorHelper + FMicroSelectorHelper
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
	PCGEXCOLLECTIONS_API
	UPCGExSelectorFactoryData* BuildLegacyFactory(FPCGExContext* InContext, const FPCGExAssetDistributionDetails& InDetails, const FPCGExMicroCacheDistributionDetails& InEntryDetails);

	/**
	 * Entry's micro cache if it can be refreshed (secondary index re-picked), null otherwise.
	 * Mesh-only: only mesh-type micro caches produce secondary indices during distribution.
	 */
	PCGEXCOLLECTIONS_API
	const PCGExMeshCollection::FMicroCache* GetRefreshableMicroCache(const FPCGExAssetCollectionEntry* InEntry);

	/** Copies Collection Map inputs to the output map pin -- for DisabledPassThroughData overrides. */
	PCGEXCOLLECTIONS_API
	void ForwardCollectionMap(FPCGContext* InContext);

	/**
	 * Resolves a soft-path-referenced root actor with a per-batch cache, falling back to the
	 * component's target actor when the path is null or fails to resolve. Soft paths point to
	 * live actors; only ResolveObject is used (no LoadSynchronous). The cache deduplicates
	 * StaticFindObject lookups across points that share the same root, and uses weak pointers
	 * to stay correct across GC if the time-sliced spawn loop yields between iterations.
	 */
	PCGEXCOLLECTIONS_API
	AActor* ResolveTargetActor(FPCGExContext* InContext, const FSoftObjectPath& InPath, TMap<FSoftObjectPath, TWeakObjectPtr<AActor>>& InOutCache);

	/**
	 * Applies PCG persistence semantics to a freshly-spawned actor:
	 * tags with DefaultPCGActorTag, Modify()/MarkPackageDirty() the actor and its
	 * level (skipped for preview/transient spawns), and registers with ManagedActors.
	 * Shared by all spawn elements (SpawnActors, loose level actors) to ensure a
	 * single consistent persistence path.
	 */
	PCGEXCOLLECTIONS_API
	void FinalizeSpawnedActor(AActor* InActor, UPCGManagedActors* InManagedActors, bool bIsPreview);

	class FSocketHelper;
	class FSelectorHelper;
	class FCollectionSource;

	/**
	 * Per-scope pick scratch storage for one FSelectorHelper -- one slot per picker op
	 * (Main + one per named category), parallel to the helper's op layout.
	 *
	 * Created via FSelectorHelper::CreateScratches once per processing scope (single-threaded,
	 * before entering the point loop of that scope), then passed back into GetEntry for every
	 * pick within the scope. Since each scope runs on one thread, ops can mutate their scratch
	 * freely without locking. Slots are null for ops that don't use scratch.
	 */
	class PCGEXCOLLECTIONS_API FSelectorScratches
	{
		friend class FSelectorHelper;

		TSharedPtr<FPCGExPickerScratchBase> Main;
		TArray<TSharedPtr<FPCGExPickerScratchBase>> ByCategory;

	public:
		/** Scratch slot for a routed pick: Main when CategorySlot < 0, the category's slot otherwise. */
		FPCGExPickerScratchBase* GetSlot(const int32 CategorySlot) const
		{
			return CategorySlot < 0 ? Main.Get() : ByCategory[CategorySlot].Get();
		}
	};

	/**
	 * Per-scope pick scratch storage for a whole FCollectionSource -- one FSelectorScratches
	 * per underlying helper. Created via FCollectionSource::CreateScratches once per scope;
	 * consumers route the right per-helper set via GetFor(Helper).
	 */
	class PCGEXCOLLECTIONS_API FSourceScratches
	{
		friend class FCollectionSource;

		TSharedPtr<FSelectorScratches> Single;
		TMap<const FSelectorHelper*, TSharedPtr<FSelectorScratches>> ByHelper;

	public:
		const FSelectorScratches* GetFor(const FSelectorHelper* InHelper) const
		{
			if (Single)
			{
				return Single.Get();
			}
			const TSharedPtr<FSelectorScratches>* Found = ByHelper.Find(InHelper);
			return Found ? Found->Get() : nullptr;
		}
	};

	/**
	 * Per-point entry picker. Reads distribution settings (index/random/weighted) and
	 * optional category filtering, then picks entries from a collection's cache.
	 *
	 * Usage:
	 *   auto Helper = MakeShared<FSelectorHelper>(Collection, DistributionDetails);
	 *   Helper->Init(DataFacade);
	 *   // In parallel loop:
	 *   FPCGExEntryAccessResult Result = Helper->GetEntry(PointIndex, Seed);
	 *
	 * Category support: when bUseCategories is enabled, picks are restricted to the named
	 * sub-category within the cache. If the picked entry is a subcollection, recursion
	 * continues into it via GetEntryWeightedRandom.
	 */
	class PCGEXCOLLECTIONS_API FSelectorHelper : public TSharedFromThis<FSelectorHelper>
	{
	protected:
		PCGExAssetCollection::FCache* Cache = nullptr;
		UPCGExAssetCollection* Collection = nullptr;

		// Effective state resolved at Init time. In Legacy mode, a transient built-in factory
		// is synthesized from Details; in External mode, the caller-provided factory is used.
		const UPCGExSelectorFactoryData* ActiveFactory = nullptr;

		TSharedPtr<PCGExDetails::TSettingValue<FName>> CategoryGetter;
		TSharedPtr<FPCGExEntryPickerOperation> MainPickerOp;

		// Parallel to Cache->CategoryNameToIndex. Slots may be null when the op's PrepareForData failed --
		// ResolvePickerForPoint treats null as "fall back to Main per MissingCategoryBehavior".
		TArray<TSharedPtr<FPCGExEntryPickerOperation>> CategoryPickerOpsByIndex;

		// Optional cache for collection-derived shared state. Typically supplied by the consumer
		// context (mirrors FPickPacker lifetime pattern). When null, ops self-build as before.
		TSharedPtr<FSelectorSharedDataCache> SharedDataCache;

		/**
		 * Resolve which picker op applies to a given point (category-aware, with MissingCategoryBehavior fallback).
		 * OutCategorySlot receives -1 for the Main op, or the category index for a category op --
		 * used to route the matching scratch slot.
		 */
		const FPCGExEntryPickerOperation* ResolvePickerForPoint(int32 PointIndex, int32& OutCategorySlot) const;

		/**
		 * Shared pre-resolve routing: resolve the point's op (same category routing as GetEntry),
		 * bail unless it opted in, and resolve its scratch slot. Returns null when the point has
		 * no pre-resolving op.
		 */
		FPCGExEntryPickerOperation* ResolvePreResolveOp(int32 PointIndex, const FSelectorScratches* Scratches, FPCGExPickerScratchBase*& OutScratch) const;

	public:
		FPCGExAssetDistributionDetails Details;

		explicit FSelectorHelper(UPCGExAssetCollection* InCollection, const FPCGExAssetDistributionDetails& InDetails);

		/**
		 * Wire a context-scoped shared-data cache. Call before Init. Ops will receive cached
		 * collection-derived state via FSelectorSharedDataCache::GetOrBuild instead of self-building.
		 */
		void SetSharedDataCache(TSharedPtr<FSelectorSharedDataCache> InCache)
		{
			SharedDataCache = InCache;
		}

		/**
		 * Initialize the helper with a data facade and optional external selector factory.
		 * @param InDataFacade Data facade to read per-point attributes from.
		 * @param ExternalFactory When provided (External mode), drives picking instead of the inline Details.
		 * @return true if initialization successful.
		 */
		bool Init(const TSharedRef<PCGExData::FFacade>& InDataFacade, const UPCGExSelectorFactoryData* ExternalFactory = nullptr);

		/** Active factory (either the External one passed to Init, or the transient built-in built from Details in Legacy mode). */
		const UPCGExSelectorFactoryData* GetActiveFactory() const
		{
			return ActiveFactory;
		}

		/**
		 * Create per-scope scratch storage for this helper's picker ops. Call once per processing
		 * scope (single-threaded, before entering the scope's point loop) and pass the result into
		 * GetEntry for every pick in that scope. Returns null when no op wants scratch -- passing
		 * null Scratches to GetEntry is always valid (ops fall back to per-pick local buffers).
		 * @param MaxPointsInScope Upper bound of points the scope will process; forwarded to ops.
		 */
		TSharedPtr<FSelectorScratches> CreateScratches(int32 MaxPointsInScope) const;

		// ========== Deterministic pre-resolve (see FPCGExEntryPickerOperation contract) ==========

		/** True when any of this helper's ops needs the pre-resolve passes. */
		bool AnyPickerWantsPreResolve() const;

		/** Allocate pre-resolve storage on every opted-in op. Single-threaded, once per facade. */
		void BeginPreResolve(int32 NumPoints) const;

		/**
		 * Parallel pass: route the point to its op (same category routing as GetEntry) and record
		 * its first choice. No-op when the routed op didn't opt in.
		 */
		void PreResolveFirstChoice(int32 PointIndex, int32 Seed, const FSelectorScratches* Scratches) const;

		/**
		 * Sequential pass: route the point to its op and commit its pick, claiming capacity.
		 * MUST be called in ascending point-index order from a single thread.
		 */
		void CommitPreResolve(int32 PointIndex, int32 Seed, const FSelectorScratches* Scratches) const;

		/**
		 * Get an entry for a specific point
		 * @param PointIndex Index of the point
		 * @param Seed Random seed for this point
		 * @param bFlattenSubCollections
		 * @param Scratches Optional per-scope scratch set from CreateScratches. May be null.
		 * @return Access result containing entry and host collection
		 */
		FPCGExEntryAccessResult GetEntry(int32 PointIndex, int32 Seed, const bool bFlattenSubCollections = false, const FSelectorScratches* Scratches = nullptr) const;

		/**
		 * Get an entry with tag inheritance
		 * @param PointIndex Index of the point
		 * @param Seed Random seed for this point
		 * @param TagInheritance Bitmask of EPCGExAssetTagInheritance flags
		 * @param OutTags Set to append inherited tags to
		 * @param bFlattenSubCollections
		 * @param Scratches Optional per-scope scratch set from CreateScratches. May be null.
		 * @return Access result containing entry and host collection
		 */
		FPCGExEntryAccessResult GetEntry(int32 PointIndex, int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags, const bool bFlattenSubCollections = false, const FSelectorScratches* Scratches = nullptr) const;

		/** Get the underlying collection */
		UPCGExAssetCollection* GetCollection() const
		{
			return Collection;
		}

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
	 *   auto MicroHelper = MakeShared<FMicroSelectorHelper>(MicroDistDetails);
	 *   MicroHelper->Init(DataFacade);
	 *   // In parallel loop:
	 *   int32 Pick = MicroHelper->GetPick(Entry->MicroCache.Get(), PointIndex, Seed);
	 *   // Pick is then passed to ApplyMaterials() or packed as SecondaryIndex
	 */
	class PCGEXCOLLECTIONS_API FMicroSelectorHelper : public TSharedFromThis<FMicroSelectorHelper>
	{
	protected:
		TSharedPtr<FPCGExMicroEntryPickerOperation> PickerOp;

	public:
		FPCGExMicroCacheDistributionDetails Details;

		explicit FMicroSelectorHelper(const FPCGExMicroCacheDistributionDetails& InDetails);

		/**
		 * @param InDataFacade Data facade to read per-point attributes from.
		 * @param ExternalFactory When provided (External mode), drives micro picking. Legacy mode synthesizes a transient factory from Details.
		 */
		bool Init(const TSharedRef<PCGExData::FFacade>& InDataFacade, const UPCGExSelectorFactoryData* ExternalFactory = nullptr);

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
	 * The attribute set contains two attributes per collection:
	 *   - Tag_CollectionIdx (int32): packed collection identifier
	 *   - Tag_CollectionPath (FSoftObjectPath): collection asset path for loading
	 *
	 * GetPickIdx() is a pure hash computation -- it does not register the collection.
	 * Callers MUST call RegisterCollection() at init (single-threaded) for every collection
	 * that can appear as a Host at runtime. RegisterCollection pulls the full flat host set
	 * from the collection's cache, so a single call covers the entire nested-collection tree.
	 * Failing to register a host that appears in a pick hash will cause PackToDataset to omit
	 * it, breaking downstream unpacking.
	 *
	 * Usage:
	 *   // In Boot / Process (single-threaded):
	 *   Packer = MakeShared<FPickPacker>();
	 *   Packer->RegisterCollection(TopLevelCollection);  // covers subcollections via FlatHosts
	 *   // In ProcessPoints (parallel, lock-free):
	 *   uint64 Hash = Packer->GetPickIdx(EntryHost, Staging.InternalIndex, SecondaryIndex);
	 *   HashWriter->SetValue(Index, Hash);
	 *   // After processing:
	 *   UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
	 *   Packer->PackToDataset(OutputSet);
	 */
	/**
	 * Canonical pick-hash layout -- the ONE definition shared by FPickPacker, FPickUnpacker
	 * and any node that rewrites picks (e.g. Staging : Swap):
	 *
	 *   uint64 = H64(CollectionGUID, H32(RawEntryIndex, SecondaryIndex + 1))
	 *
	 * Raw entry indices are 16-bit BY DESIGN (collections beyond that are out of contract);
	 * SecondaryIndex is stored +1 so 0 means "none" (-1). Change the layout here and nowhere else.
	 */
	namespace PickHash
	{
		FORCEINLINE uint64 Pack(const uint32 InCollectionGUID, const uint16 InRawEntryIndex, const int16 InSecondaryIndex = -1)
		{
			return PCGEx::H64(InCollectionGUID, PCGEx::H32(InRawEntryIndex, InSecondaryIndex + 1));
		}

		FORCEINLINE uint32 GetCollectionGUID(const uint64 InHash)
		{
			return PCGEx::H64A(InHash);
		}

		FORCEINLINE uint16 GetRawEntryIndex(const uint64 InHash)
		{
			return static_cast<uint16>(PCGEx::H32A(PCGEx::H64B(InHash)));
		}

		FORCEINLINE int16 GetSecondaryIndex(const uint64 InHash)
		{
			return static_cast<int16>(PCGEx::H32B(PCGEx::H64B(InHash))) - 1;
		}

		/**
		 * Collection+entry identity key with the secondary pick stripped: H64(CollectionGUID, RawEntryIndex).
		 * The canonical key for per-entry maps matched against staged picks (Swap contributions,
		 * Distribute sub-collection routing). The uint16 parameter enforces the 16-bit raw-index
		 * contract at build sites -- callers with wider indices must truncate explicitly, mirroring Pack().
		 */
		FORCEINLINE uint64 MakeEntryKey(const uint32 InCollectionGUID, const uint16 InRawEntryIndex)
		{
			return PCGEx::H64(InCollectionGUID, InRawEntryIndex);
		}

		/** Entry identity key of a packed pick hash -- see MakeEntryKey. */
		FORCEINLINE uint64 GetEntryKey(const uint64 InHash)
		{
			return MakeEntryKey(GetCollectionGUID(InHash), GetRawEntryIndex(InHash));
		}
	}

	class PCGEXCOLLECTIONS_API FPickPacker : public TSharedFromThis<FPickPacker>
	{
		TMap<const UPCGExAssetCollection*, uint32> CollectionMap;
		mutable FRWLock AssetCollectionsLock;

	public:
		FPickPacker() = default;
		explicit FPickPacker(FPCGContext* InContext);

		/**
		 * Register a collection and every host reachable from it (via FlatHosts). Idempotent.
		 * Must be called at init time for every top-level collection that can surface as a
		 * Host during GetEntry. Thread-safe but intended for single-threaded init paths.
		 */
		void RegisterCollection(UPCGExAssetCollection* InCollection);

		/**
		 * Compute the packed identifier for a collection entry pick. Pure hash -- no lock,
		 * no map lookup. InCollection must have been passed to RegisterCollection (or reached
		 * via another collection's FlatHosts) prior to PackToDataset, otherwise the downstream
		 * mapping will be missing.
		 * IMPORTANT: InIndex must be a RAW Entries array index (e.g. Staging.InternalIndex),
		 * not a cache-adjusted index. The unpacker uses GetEntryRaw() to resolve it.
		 */
		FORCEINLINE uint64 GetPickIdx(const UPCGExAssetCollection* InCollection, int16 InIndex, int16 InSecondaryIndex) const
		{
			return PickHash::Pack(InCollection->GetCollectionGUID(), static_cast<uint16>(InIndex), InSecondaryIndex);
		}

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
		TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> CollectionsHandles;
		int32 NumUniqueEntries = 0;
		const UPCGBasePointData* PointData = nullptr;

	public:
		TMap<int64, TSharedPtr<TArray<int32>>> HashedPartitions;
		TMap<int64, int32> IndexedPartitions;

		FPickUnpacker() = default;

		bool HasValidMapping() const
		{
			return !CollectionMap.IsEmpty();
		}

		/** Get read-only access to the collection map */
		const TMap<uint32, UPCGExAssetCollection*>& GetCollections() const
		{
			return CollectionMap;
		}

		/**
		 * Register every unpacked collection into the given packer. Consumers that emit a superset
		 * output map (pass-through pick hashes must stay resolvable downstream -- e.g. Staging : Swap,
		 * Staging : Distribute in CollectionMap mode) call this once after UnpackPin, before adding
		 * their own contributions.
		 */
		void RegisterCollectionsTo(FPickPacker& InPacker) const;

		/** Unpack collection mappings from an attribute set */
		bool UnpackDataset(FPCGContext* InContext, const UPCGParamData* InAttributeSet);

		/** Unpack from a specific input pin */
		void UnpackPin(FPCGContext* InContext, FName InPinLabel = NAME_None);

		/**
		 * Build point partitions from point data.
		 *
		 * Templated on the instance-list type so the same partitioning logic works for both
		 * static (FPCGMeshInstanceList) and skinned (FPCGSkinnedMeshInstanceList) selectors.
		 * Field-name and PointData-type differences are absorbed by TInstanceListTraits<T>.
		 * Explicit instantiations live in the .cpp for both built-in list types.
		 */
		template <typename T>
		bool BuildPartitions(const UPCGBasePointData* InPointData, TArray<T>& InstanceLists);

		template <typename T>
		void InsertEntry(const uint64 EntryHash, const int32 EntryIndex, TArray<T>& InstanceLists);

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
	 * FSelectorHelper + FMicroSelectorHelper pairs and routes TryGetHelpers()
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
	 *   FSelectorHelper* Helper; FMicroSelectorHelper* MicroHelper;
	 *   if (Source->TryGetHelpers(Index, Helper, MicroHelper)) { ... }
	 */
	class PCGEXCOLLECTIONS_API FCollectionSource : public TSharedFromThis<FCollectionSource>
	{
		TSharedPtr<FSelectorHelper> Helper;
		TSharedPtr<FMicroSelectorHelper> MicroHelper;

		// For mapped sources
		TArray<TSharedPtr<FSelectorHelper>> Helpers;
		TArray<TSharedPtr<FMicroSelectorHelper>> MicroHelpers;
		TMap<PCGExValueHash, int32> Indices;

		TSharedPtr<TArray<PCGExValueHash>> Keys;
		TSharedPtr<PCGExData::FFacade> DataFacade;
		UPCGExAssetCollection* SingleSource = nullptr;

		// Optional shared-data cache (typically from the consumer context). Plumbed into each
		// FSelectorHelper at Init so collection-derived state is built once and reused.
		TSharedPtr<FSelectorSharedDataCache> SharedDataCache;

	public:
		FPCGExAssetDistributionDetails DistributionSettings;
		FPCGExMicroCacheDistributionDetails EntryDistributionSettings;

		explicit FCollectionSource(const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		/** Wire a context-scoped shared-data cache. Call before Init. */
		void SetSharedDataCache(TSharedPtr<FSelectorSharedDataCache> InCache)
		{
			SharedDataCache = InCache;
		}

		/** Initialize with a single collection. ExternalFactory drives picking in External mode; nullptr falls back to Legacy inline details. */
		bool Init(UPCGExAssetCollection* InCollection, const UPCGExSelectorFactoryData* ExternalFactory = nullptr);

		/** Initialize with a mapped collection source. ExternalFactory drives picking for all collections in External mode. */
		bool Init(const TMap<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& InMap, const TSharedPtr<TArray<PCGExValueHash>>& InKeys, const UPCGExSelectorFactoryData* ExternalFactory = nullptr);

		/**
		 * Create per-scope scratch storage covering every helper this source wraps. Call once per
		 * processing scope (single-threaded, before the scope's point loop); route the per-helper
		 * set via FSourceScratches::GetFor(Helper) when calling GetEntry. Returns null when no op
		 * across any helper wants scratch -- null is always safe to skip.
		 */
		TSharedPtr<FSourceScratches> CreateScratches(int32 MaxPointsInScope) const;

		/** True when any op across the wrapped helpers needs the deterministic pre-resolve passes. */
		bool AnyPickerWantsPreResolve() const;

		/** Allocate pre-resolve storage on every opted-in op across the wrapped helpers. */
		void BeginPreResolve(int32 NumPoints) const;

		/**
		 * Get helpers for a specific point index
		 * @param Index Point index
		 * @param OutHelper Output: distribution helper
		 * @param OutMicroHelper Output: micro distribution helper (may be null)
		 * @return true if valid helpers found
		 */
		bool TryGetHelpers(int32 Index, FSelectorHelper*& OutHelper, FMicroSelectorHelper*& OutMicroHelper);

		/** Check if this is a single source */
		bool IsSingleSource() const
		{
			return SingleSource != nullptr;
		}

		/** Get the single source collection (if applicable) */
		UPCGExAssetCollection* GetSingleSource() const
		{
			return SingleSource;
		}

		/**
		 * Pre-register every collection this source can surface as a Host with the given
		 * packer. Call once after Init(), before entering a parallel ProcessPoints loop.
		 * The packer's RegisterCollection pulls each collection's FlatHosts set, so nested
		 * sub-collections are handled automatically.
		 */
		void RegisterCollectionsTo(FPickPacker& Packer) const;

		/**
		 * Pre-register every leaf entry this source can surface as a Host with the given
		 * socket helper, then seal it for lock-free Add() in the parallel loop. Call once
		 * after Init(), before entering a parallel ProcessPoints loop. For multi-source
		 * scenarios, call PreRegisterCollection manually for each and Seal() at the end.
		 */
		void RegisterSocketsTo(FSocketHelper& SocketHelper) const;
	};

	/**
	 * Collection-aware socket helper. Extracts socket transforms from collection entries'
	 * staging data and builds per-entry socket point sets. Call Compile() after processing
	 * to output socket points to a FPointIOCollection.
	 *
	 * Usage contract:
	 *   1. Construct.
	 *   2. RegisterCollection(...) once for each top-level collection (covers subcollections
	 *      via FlatHosts). Single-threaded init.
	 *   3. Parallel Add() from ProcessPoints -- lock-free.
	 *   4. Compile() to produce socket outputs.
	 *
	 * Add() is always lock-free and assumes every (Host, EntryIndex) pair it sees has been
	 * pre-registered. Unregistered entries are a programming error -- Add() is a no-op in
	 * that case, guarded by checkSlow in debug builds.
	 */
	class PCGEXCOLLECTIONS_API FSocketHelper : public PCGExStaging::FSocketHelper
	{
	public:
		explicit FSocketHelper(const FPCGExSocketOutputDetails* InDetails, const int32 InNumPoints);

		void Add(const int32 Index, const uint64 EntryHash, const FPCGExAssetCollectionEntry* Entry);

		/**
		 * Populate InfosKeys + SocketInfosList for every leaf entry reachable from this
		 * collection (self + all FlatHosts). Idempotent; safe to call multiple times for
		 * different top-level collections. Must complete before any parallel Add() call.
		 */
		void RegisterCollection(UPCGExAssetCollection* InCollection);
	};
}
