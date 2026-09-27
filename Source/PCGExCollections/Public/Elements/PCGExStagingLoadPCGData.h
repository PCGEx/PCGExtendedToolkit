// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Factories/PCGExFactories.h"
#include "Fitting/PCGExFitting.h"
#include "Helpers/PCGExCollectionsHelpers.h"

#include "PCGExStagingLoadPCGData.generated.h"

class UPCGLandscapeData;
class UPCGVolumeData;
class UPCGSurfaceData;
class UPCGPrimitiveData;
class UPCGPolyLineData;
class UPCGSplineData;
class UPCGDataAsset;
class UPCGExPCGDataAssetCollection;
struct FPCGExPCGDataAssetCollectionEntry;

namespace PCGExPCGDataAssetLoader
{
	class FProcessor;
	class FBatch;
}


namespace PCGExPCGDataAssetLoader
{
	/** Result of attempting to transform spatial data */
	enum class ETransformResult : uint8
	{
		Success     = 0,
		Unsupported = 1,
		Failed      = 2,
	};

	struct FSpatialTransformResult
	{
		ETransformResult Result = ETransformResult::Failed;
		TSharedPtr<PCGExMT::FTask> Task = nullptr;

		FSpatialTransformResult() = default;
		explicit FSpatialTransformResult(ETransformResult InResult);
		explicit FSpatialTransformResult(const TSharedPtr<PCGExMT::FTask>& InTask);
	};

	static FSpatialTransformResult PrepareTransformTask(UPCGSpatialData* InData, const FTransform& InTransform);

	/** Where an asset datum goes for one target */
	enum class ERoute : uint8
	{
		Skip   = 0,
		Unique = 1, // Asset-owned, output once: non-spatial data, or passthrough without forwarding
		Merge  = 2, // bMergePointOutputs: replicated into the entry's merged output
		Copy   = 3, // Duplicated per target (transformed unless passthrough); cluster pair tags are remapped
	};

	/** Total order of outputs within a pin: one datum per target per asset index, so no two outputs tie */
	struct FOutputOrder
	{
		int32 Rank = 1; // 0 = non-spatial asset data, output ahead of everything else
		int32 Input = 0;
		int32 Point = 0;
		int32 Datum = 0; // Index in the asset's data collection

		bool operator<(const FOutputOrder& Other) const
		{
			if (Rank != Other.Rank) { return Rank < Other.Rank; }
			if (Input != Other.Input) { return Input < Other.Input; }
			if (Point != Other.Point) { return Point < Other.Point; }
			return Datum < Other.Datum;
		}
	};

	struct FOutputEntry
	{
		FPCGTaggedData TaggedData;
		FOutputOrder Order;
		bool bOwned = false; // Created by this node; asset-owned data is never staged mutable
	};

	/** Where a unique datum's entry lives, and the order of the registrant that placed it */
	struct FUniqueSlot
	{
		FOutputOrder Order;
		FName Pin = NAME_None;
		int32 Index = INDEX_NONE;
	};
}

/**
 * Spawns PCGDataAsset contents onto staged points.
 * Works with data staged by the Asset Staging node using Collection Map output.
 *
 * Also accepts attribute sets carrying the staged entry hash (e.g. Get Collection Data's Data output):
 * each row is loaded and its asset contents are output as-is, without duplication or transform.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "spawn pcgdata asset staged", PCGExNodeLibraryDoc="staging/staging-load-pcgdata"))
class UPCGExPCGDataAssetLoaderSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PCGDataAssetLoader, "Staging : Load PCGData", "Loads and spawns PCGDataAsset contents from staged points, or outputs them raw from staged attribute sets.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

	/** Accept attribute sets on the main pin: rows are converted to temp identity points for hash
	 *  resolution, and their loaded contents are output raw (see FProcessor::bPassthrough). */
	virtual PCGExData::EIOHandling GetMainDataHandling() const override
	{
		return PCGExData::EIOHandling::Dynamic;
	}

protected:
	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Staging layer this node reads staged picks from. None = default layer (PCGEx/CollectionEntry); otherwise the layer name is appended (PCGEx/CollectionEntry/<layer>). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable), AdvancedDisplay)
	FName StagingLayer = NAME_None;

	FName GetEntryIdxAttributeName() const { return PCGExCollections::Labels::EntryIdxName(StagingLayer); }

	/**
	 * Custom output pins for routing data by pin name.
	 * Data from the PCGDataAsset will be routed to matching pins by exact name.
	 * Data that doesn't match any custom pin goes to the default "Out" pin.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (TitleProperty = "{Label}"))
	TArray<FPCGPinProperties> CustomOutputPins;

	/** If enabled, will refresh seeds on output points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bRefreshSeeds = false;

	/** If enabled, will not output empty data, even if they have possibly meaningful @Data attributes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitEmptyData = true;

	/** Merge every target's copy of an asset's point data into one output per input; cluster data is never
	 *  merged. Forwarded target attributes land per element, replacing same-named source attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMergePointOutputs = false;

	/** If enabled, only spawn data from the PCGDataAsset that matches these tags. Empty means all data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable))
	bool bFilterByTags = false;

	/** Tags to include. If empty, all data is included. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable, EditCondition="bFilterByTags"))
	TSet<FString> IncludeTags;

	/** Tags to exclude. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable, EditCondition="bFilterByTags"))
	TSet<FString> ExcludeTags;

	/** Which target attributes to forward on spawned point data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta = (PCG_Overridable))
	FPCGExForwardDetails TargetsForwarding;

	/** If enabled, forward input data tags to spawned data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta = (PCG_Overridable))
	bool bForwardInputTags = true;

	/** When enabled, scans loaded data assets for embedded CollectionMap entries,
	 *  merges them into a single output, and strips them from duplicated data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMergeEmbeddedCollectionMaps = true;

	/** Quiet warnings about unsupported spatial data types that cannot be transformed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_Overridable))
	bool bQuietUnsupportedTypeWarnings = false;

	/** Quiet warnings about missing or invalid entries */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_Overridable))
	bool bQuietInvalidEntryWarnings = false;
};

/**
 * Shared asset pool for loading PCGDataAssets once across all processors.
 * Uses entry hash (unique to collection/entry pair) as key.
 * Thread-safe registration during parallel processing, single consolidated load after.
 */
class PCGEXCOLLECTIONS_API FPCGExSharedAssetPool : public TSharedFromThis<FPCGExSharedAssetPool>
{
protected:
	mutable FRWLock PoolLock;

	// Entry hash -> Entry pointer mapping (built during parallel phase)
	TMap<uint64, const FPCGExPCGDataAssetCollectionEntry*> EntryMap;

	// Entry pointer -> Loaded asset (populated after load)
	TMap<const FPCGExPCGDataAssetCollectionEntry*, TObjectPtr<UPCGDataAsset>> LoadedAssets;

	TSharedPtr<FStreamableHandle> LoadHandle;

public:
	using FOnLoadEnd = std::function<void(const bool bSuccess)>;

	FPCGExSharedAssetPool() = default;
	~FPCGExSharedAssetPool();

	/**
	 * Thread-safe: Register an entry by its hash.
	 * Called during parallel ProcessPoints from any processor.
	 */
	void RegisterEntry(uint64 EntryHash, const FPCGExPCGDataAssetCollectionEntry* Entry);

	/**
	 * Load all registered unique assets. Call once after all processors complete initial processing.
	 */
	void LoadAllAssets(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FOnLoadEnd&& OnLoadEnd);

	/**
	 * Get loaded asset by entry hash.
	 * Call after LoadAllAssets() has completed.
	 */
	UPCGDataAsset* GetAsset(uint64 EntryHash) const;

	/**
	 * Get loaded asset by entry pointer.
	 */
	UPCGDataAsset* GetAsset(const FPCGExPCGDataAssetCollectionEntry* Entry) const;

	/**
	 * Check if pool has any registered entries.
	 */
	bool HasEntries() const;

	/**
	 * Get number of unique entries.
	 */
	int32 GetNumEntries() const;

	/**
	 * Get read-only access to the entry map.
	 */
	const TMap<uint64, const FPCGExPCGDataAssetCollectionEntry*>& GetEntryMap() const
	{
		return EntryMap;
	}
};

struct FPCGExPCGDataAssetLoaderContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExPCGDataAssetLoaderElement;
	friend class PCGExPCGDataAssetLoader::FProcessor;
	friend class PCGExPCGDataAssetLoader::FBatch;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionUnpacker;

	// Shared asset pool - all processors register entries here, single load
	TSharedPtr<FPCGExSharedAssetPool> SharedAssetPool;

	// Custom output pin names for routing
	TSet<FName> CustomPinNames;

	TMap<FName, TArray<PCGExPCGDataAssetLoader::FOutputEntry>> OutputByPin;
	mutable FRWLock OutputLock;

	// ERoute::Unique outputs by data UID: asset-owned data is output once, never duplicated
	TMap<uint32, PCGExPCGDataAssetLoader::FUniqueSlot> UniqueData;
	mutable FRWLock UniqueDataLock;

	// Merged collection map from embedded CollectionMap entries (when bMergeEmbeddedCollectionMaps)
	TSharedPtr<PCGExCollections::FPickPacker> MergedMapPacker;

	/** Register data this node created */
	void RegisterOutput(const FPCGTaggedData& InTaggedData, const PCGExPCGDataAssetLoader::FOutputOrder& InOrder);

	/** Register asset-owned data as-is, once per unique data (dedupes on data UID).
	 *  The registrant first in output order wins, tags included, whichever input completes first. */
	void RegisterUniqueData(const FPCGTaggedData& InTaggedData, const PCGExPCGDataAssetLoader::FOutputOrder& InOrder);

	/** Stage every registered output, sorted per pin; only data this node created is staged mutable */
	void StageRegisteredOutputs();

protected:
	/** Output pin routing: custom pin on an exact name match, else the default pin with a Pin: tag */
	FPCGTaggedData ResolveOutput(const FPCGTaggedData& InTaggedData) const;

	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPCGDataAssetLoaderElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PCGDataAssetLoader)

	virtual void DisabledPassThroughData(FPCGContext* Context) const override;

	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPCGDataAssetLoader
{
	const FName OutputPinDefault = TEXT("Out");

	/** Tracks cluster ID remapping for a single point's spawned data */
	struct FClusterIdRemapper
	{
		// Original cluster ID -> New cluster ID
		TMap<int32, int32> IdMap;

		// Counter for generating new IDs
		int32& SharedIdCounter;

		explicit FClusterIdRemapper(int32& InSharedCounter)
			: SharedIdCounter(InSharedCounter)
		{
		}

		/** Get or create a new ID for the given original ID */
		FORCEINLINE int32 GetRemappedId(int32 OriginalId)
		{
			if (int32* Found = IdMap.Find(OriginalId))
			{
				return *Found;
			}

			int32 NewId = ++SharedIdCounter;
			IdMap.Add(OriginalId, NewId);
			return NewId;
		}
	};

	/** bMergePointOutputs: every target of one input that resolves to the same asset datum, replicated into one output */
	struct FMergeGroup
	{
		FPCGTaggedData Source;
		TArray<int32> TargetIndices;

		// First target's order: the merged output sits where that target's copy would
		FOutputOrder Order;

		// In = source datum, Out = merged output
		TSharedPtr<PCGExData::FPointIO> MergedIO;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPCGDataAssetLoaderContext, UPCGExPCGDataAssetLoaderSettings>
	{
		friend class FBatch;

	protected:
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

		// bMergePointOutputs: one group per asset datum entry, in first-target order
		TArray<TSharedPtr<FMergeGroup>> MergeGroups;
		TMap<const FPCGTaggedData*, int32> MergeGroupByEntry;

		// Per-point entry hash (0 for invalid/filtered points)
		TArray<uint64> PointEntryHashes;

		// Forward handler (created after facade is available)
		TSharedPtr<PCGExData::FDataForwardHandler> ForwardHandler;

		// Cluster pair ID counter, shared across this input's targets. Starts at the input's reserved base
		// (FBatch::ReserveClusterIds), so IDs are unique across inputs and never depend on completion order.
		int32 ClusterIdCounter = 0;

		// Input tags forwarded onto outputs, minus the cluster pairing tags: every copy keeps its own pair ID
		TSet<FString> ForwardedInputTags;

		// True when the input is a converted attribute set: loaded contents are output as-is
		// (no duplicate, no transform), one instance per unique data. Forwarding still duplicates.
		bool bPassthrough = false;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override = default;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;

	protected:
		/** Check if tagged data passes tag filters */
		bool PassesTagFilter(const FPCGTaggedData& InTaggedData) const;

		/** bMergePointOutputs: point data that is merged across targets instead of duplicated per target */
		bool ShouldMerge(const FPCGTaggedData& InTaggedData) const;

		/** Where an asset datum goes. The single decision behind both emission and the cluster ID reservation. */
		ERoute RouteDatum(const FPCGTaggedData& InTaggedData) const;

		/** Loaded asset a point spawns; null when the point is filtered out, unstaged, or its asset failed to load */
		UPCGDataAsset* GetTargetAsset(int32 PointIndex) const;

		/** Cluster pair IDs CompleteWork will consume: one per distinct pair ID among the copied data of each target */
		int32 GetClusterIdDemand() const;

		void QueueMerge(int32 PointIndex, const FOutputOrder& InOrder, const FPCGTaggedData& InTaggedData);

		/** Creates and registers the group's output; false when there is nothing to replicate. */
		bool StartMergeGroup(const TSharedPtr<FMergeGroup>& Group);
		void ReplicateGroup(const FMergeGroup& Group) const;

		/** Process a single tagged data item for a point */
		FSpatialTransformResult ProcessTaggedData(int32 PointIndex, int32 DatumIndex, const FTransform& TargetTransform, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper);

		/** Passthrough: output asset data as-is (deduped), or a non-transformed duplicate when attribute forwarding is enabled */
		void ProcessPassthroughData(int32 PointIndex, const FOutputOrder& InOrder, ERoute Route, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper);

		/** Check if data has PCGEx cluster tags and remap them */
		void RemapClusterTags(TSet<FString>& Tags, FClusterIdRemapper& ClusterRemapper) const;
	};

	class FBatch final : public PCGExPointsMT::TBatch<FProcessor>
	{
		TWeakPtr<PCGExMT::FAsyncToken> LoadingToken;

	public:
		explicit FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
			: TBatch(InContext, InPointsCollection)
		{
		}

		virtual void CompleteWork() override;
		void OnLoadAssetsComplete(const bool bSuccess);

	protected:
		/** Gives each input a contiguous block of cluster pair IDs, in input order, before any processor completes */
		void ReserveClusterIds();
	};
}
