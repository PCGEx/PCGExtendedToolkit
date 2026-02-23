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

	static FSpatialTransformResult PrepareTransformTask(UPCGSpatialData* InData, const FTransform& InTransform, bool bOmitIfEmpty = false);
}

/**
 * Spawns PCGDataAsset contents onto staged points.
 * Works with data staged by the Asset Staging node using Collection Map output.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "spawn pcgdata asset staged", PCGExNodeLibraryDoc="staging/staging-load-pcgdata"))
class UPCGExPCGDataAssetLoaderSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PCGDataAssetLoader, "Staging : Load PCGData", "Loads and spawns PCGDataAsset contents from staged points.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters, false)
#endif

protected:
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/**
	 * Custom output pins for routing data by pin name.
	 * Data from the PCGDataAsset will be routed to matching pins by exact name.
	 * Data that doesn't match any custom pin goes to the default "Out" pin.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (TitleProperty = "Label"))
	TArray<FPCGPinProperties> CustomOutputPins;

	/** If enabled, will refresh seeds on output points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bRefreshSeeds = false;
	
	/** If enabled, will not output empty data, even if they have possibly meaningful @Data attributes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitEmptyData = true;
	
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
	bool bMergeEmbeddedCollectionMaps = false;

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

	// Streamable handle
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
	const TMap<uint64, const FPCGExPCGDataAssetCollectionEntry*>& GetEntryMap() const { return EntryMap; }
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

	// Output data organized by pin
	TMap<FName, TArray<FPCGTaggedData>> OutputByPin;
	TMap<uint32, int32> OutputIndices;
	mutable FRWLock OutputLock;

	// Non-spatial data (forwarded once per unique asset, not duplicated)
	TSet<uint32> UniqueNonSpatialUIDs;
	mutable FRWLock NonSpatialLock;

	// Merged collection map from embedded CollectionMap entries (when bMergeEmbeddedCollectionMaps)
	TSharedPtr<PCGExCollections::FPickPacker> MergedMapPacker;

	/** Register output data to appropriate pin */
	void RegisterOutput(const FPCGTaggedData& InTaggedData, bool bAddPinTag, const int32 InIndex);

	/** Register non-spatial data (once per unique asset) */
	void RegisterNonSpatialData(const FPCGTaggedData& InTaggedData, const int32 InIndex);

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPCGDataAssetLoaderElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PCGDataAssetLoader)

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
			if (int32* Found = IdMap.Find(OriginalId)) { return *Found; }

			int32 NewId = ++SharedIdCounter;
			IdMap.Add(OriginalId, NewId);
			return NewId;
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPCGDataAssetLoaderContext, UPCGExPCGDataAssetLoaderSettings>
	{
	protected:
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

		// Per-point entry hash (0 for invalid/filtered points)
		TArray<uint64> PointEntryHashes;

		// Forward handler (created after facade is available)
		TSharedPtr<PCGExData::FDataForwardHandler> ForwardHandler;

		// Shared counter for generating unique cluster IDs across all points
		int32 ClusterIdCounter = 0;

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

		/** Process a single tagged data item for a point */
		FSpatialTransformResult ProcessTaggedData(int32 PointIndex, const FTransform& TargetTransform, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper);

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
	};
}
