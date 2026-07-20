// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExRoamingAssetCollectionDetails.h"
#include "Details/PCGExSocketOutputDetails.h"
#include "Details/PCGExStagedTypeFilterDetails.h"
#include "Details/PCGExStagingDetails.h"
#include "Factories/PCGExFactories.h"
#include "Fitting/PCGExFitting.h"

#include "PCGExStagingDistribute.generated.h"

namespace PCGExCollections
{
	class FPickUnpacker;
	class FSocketHelper;
	class FCollectionSource;
	class FMicroSelectorHelper;
	class FPickPacker;
	class FSelectorSharedDataCache;
}

namespace PCGExStaging
{
	class FCollectionSource;
}

struct FPCGExAssetCollectionEntry;

namespace PCGExMeshCollection
{
	class FMicroCache;
}

namespace PCGExAssetCollection
{
	class FMicroCache;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

UENUM()
enum class EPCGExStagingOutputMode : uint8
{
	Attributes    = 0 UMETA(DisplayName = "Point Attributes", ToolTip="Write asset data on the point"),
	CollectionMap = 1 UMETA(DisplayName = "Collection Map", ToolTip="Write collection reference & pick for later use"),
};

UENUM()
enum class EPCGExDistributeSourceMode : uint8
{
	Default       = 0 UMETA(DisplayName = "Default", ToolTip="Read the collection to distribute from an asset reference or a per-point path attribute."),
	CollectionMap = 1 UMETA(DisplayName = "Collection Map", ToolTip="Redistribute already-staged picks read from an upstream Collection Map. See Redistribution Mode for what gets redistributed; unresolvable points always pass through untouched."),
};

UENUM()
enum class EPCGExRedistributionMode : uint8
{
	Collections = 0 UMETA(DisplayName = "Redistribute Collections", ToolTip="Points whose staged entry is a sub-collection get a new pick from that sub-collection; all other points pass through untouched."),
	MicroCache  = 1 UMETA(DisplayName = "Redistribute Micro Cache", ToolTip="Keep every staged entry pick and only re-randomize the entry's micro cache selection (secondary index, e.g. mesh material variants). Points whose entry has no micro cache pass through untouched."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage prepare spawn proxy", PCGExNodeLibraryDoc="staging/staging-distribute"))
class UPCGExAssetStagingSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExAssetStagingSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(AssetStaging, "Staging : Distribute", "Distribute PCGEx Asset Collection entries to points.", FName(GetDisplayName()));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}

	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual TOptional<FPCGNodeThumbnailProxy> GetNodeThumbnail() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;
	
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

	virtual bool WantsDataStealing() const override;

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

protected:
	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get staged.", PCGExFactories::PointFilters(), false)
	//~End UPCGSettings
	
public:
	/** Where the collection(s) to distribute from come from.
	 * Default reads SourceCollection (asset reference or per-point path attribute).
	 * Collection Map reads already-staged picks from an upstream Collection Map and redistributes
	 * them according to Redistribution Mode; unresolvable points always pass through untouched. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExDistributeSourceMode SourceMode = EPCGExDistributeSourceMode::Default;

	/** What gets redistributed when consuming a Collection Map.
	 * Collections re-picks entries for points whose staged entry is a sub-collection.
	 * Micro Cache keeps every staged entry pick and only re-randomizes the entry's micro cache
	 * selection (secondary index, e.g. mesh material variants) -- lets you refresh material
	 * assignments without changing the asset distribution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="SourceMode == EPCGExDistributeSourceMode::CollectionMap", EditConditionHides))
	EPCGExRedistributionMode RedistributionMode = EPCGExRedistributionMode::Collections;

	/** Asset collection to stage from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, AllowedClasses="/Script/PCGExCollections.PCGExAssetCollection", EditCondition="SourceMode == EPCGExDistributeSourceMode::Default", EditConditionHides))
	FPCGExInputShorthandNameSoftObjectPath SourceCollection;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExStagingOutputMode OutputMode = EPCGExStagingOutputMode::CollectionMap;

#pragma region DEPRECATED
	
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExCollectionSource CollectionSource_DEPRECATED = EPCGExCollectionSource::Asset;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	TSoftObjectPtr<UPCGExAssetCollection> AssetCollection_DEPRECATED;

#pragma endregion
	
	UPROPERTY()
	FName CollectionPathAttributeName_DEPRECATED = "CollectionPath";

	/** The name of the attribute to write asset path to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode == EPCGExStagingOutputMode::Attributes", EditConditionHides))
	FName AssetPathAttributeName = "AssetPath";

	//** If enabled, doesn't go through collections recursively and assign top-level collections "as assets" */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache", EditConditionHides))
	bool bFlattenSubCollections = false;

	/** How distribution is configured for this node. 
	 * Legacy uses the inline settings below -- only set for legacy nodes.
	 * External uses a factory on the Selector input pin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	EPCGExSelectorMode SelectorMode = EPCGExSelectorMode::External;


	/** Distribution details
	 * Note : LEGACY Nodes only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Distribution", EditCondition="SelectorMode == EPCGExSelectorMode::Legacy", EditConditionHides))
	FPCGExAssetDistributionDetails DistributionSettings;

	/** Distribution details that are specific to the picked entry -- what it picks depends on the type of collection being staged.
	 * For Mesh Collections, this let you control how materials are picked.
	 * Drives Micro Cache redistribution when no Selector is connected; otherwise LEGACY Nodes only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Distribution (Micro-cache)", EditCondition="SelectorMode == EPCGExSelectorMode::Legacy || (SourceMode == EPCGExDistributeSourceMode::CollectionMap && RedistributionMode == EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	FPCGExMicroCacheDistributionDetails EntryDistributionSettings;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache", EditConditionHides))
	bool bApplyFitting = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bApplyFitting && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	FPCGExScaleToFitDetails ScaleToFit;

	/** When enabled, entries that define a Scale to Fit override (entry-local or collection-global) replace this node's Scale to Fit for their points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Entry Overrides", EditCondition="bApplyFitting && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	bool bConsiderEntryScaleToFit = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bApplyFitting && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	FPCGExJustificationDetails Justification;

	/** When enabled, entries that define a Justification override (entry-local or collection-global) replace this node's Justification for their points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Entry Overrides", EditCondition="bApplyFitting && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	bool bConsiderEntryJustification = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="bApplyFitting && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)", EditConditionHides))
	FPCGExFittingVariationsDetails Variations;


	//** If enabled, filter output based on whether a staging has been applied or not (empty entry).  Current implementation is slow.
	// Not applicable in Collection Map source mode, where unresolvable points always pass through untouched. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="SourceMode != EPCGExDistributeSourceMode::CollectionMap"))
	bool bPruneEmptyPoints = true;

	/** Filter output based on the type of collection entry. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bDoFilterEntryType = false;

	/** Lets you filter which collection type gets staged.
	 * This is most useful when using per-point collections and you want to stage only certain types of assets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, EditCondition="bDoFilterEntryType"))
	FPCGExStagedTypeFilterDetails EntryTypeFilter;

	/** Write the collection entry type (Mesh, Actor, etc.) to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteEntryType = false;

	/** Name of the FName entry type will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bWriteEntryType"))
	FName EntryTypeAttributeName = FName("EntryType");

	/** Update point scale so staged asset fits within its bounds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache", EditConditionHides))
	EPCGExWeightOutputMode WeightToAttribute = EPCGExWeightOutputMode::NoOutput;

	/** The name of the attribute to write asset weight to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="WeightToAttribute != EPCGExWeightOutputMode::NoOutput && WeightToAttribute != EPCGExWeightOutputMode::NormalizedToDensity && WeightToAttribute != EPCGExWeightOutputMode::NormalizedInvertedToDensity && (SourceMode != EPCGExDistributeSourceMode::CollectionMap || RedistributionMode != EPCGExRedistributionMode::MicroCache)"))
	FName WeightAttributeName = "AssetWeight";

	//** If enabled, will output mesh material picks. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="OutputMode != EPCGExStagingOutputMode::CollectionMap"))
	bool bOutputMaterialPicks = false;

	//** If > 0 will create dummy attributes for missing material indices up to a maximum; in order to create a full, fixed-length list of valid (yet null) attributes for the static mesh spawner material overrides. Otherwise, will only create attribute for valid indices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Fixed Max Index", EditCondition="bOutputMaterialPicks && OutputMode != EPCGExStagingOutputMode::CollectionMap", ClampMin="0"))
	int32 MaxMaterialPicks = 0;

	/** Prefix to be used for material slot picks.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, DisplayName=" └─ Prefix", EditCondition="bOutputMaterialPicks && OutputMode != EPCGExStagingOutputMode::CollectionMap"))
	FName MaterialAttributePrefix = "Mat";

	/** Output socket transforms from staged meshes as a separate point collection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoOutputSockets = false;

	/** Socket output configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_NotOverridable, DisplayName="Output Sockets", EditCondition="bDoOutputSockets"))
	FPCGExSocketOutputDetails OutputSocketDetails;

	/** Write the fitting translation offset to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteTranslation = false;

	/** Name of the FVector attribute to write fitting offset to. 
	 * This is the translation added to the point transform according to staging/justification rules.
	 * Mostly useful for offsetting spline meshes.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bWriteTranslation"))
	FName TranslationAttributeName = FName("FittingOffset");

	/** Suppress warnings when the asset collection is empty or has no valid entries. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietEmptyCollectionError = false;

	/** Micro-cache redistribution mode predicate -- all C++ sites route through this. */
	FORCEINLINE bool IsMicroRedistribution() const
	{
		return SourceMode == EPCGExDistributeSourceMode::CollectionMap && RedistributionMode == EPCGExRedistributionMode::MicroCache;
	}

#if WITH_EDITOR
	FString GetDisplayName() const;
#endif
};

struct FPCGExAssetStagingContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExAssetStagingElement;

	virtual void RegisterAssetDependencies() override;

	EPCGExStagingOutputMode OutputMode = EPCGExStagingOutputMode::CollectionMap;

	TSharedPtr<PCGEx::TAssetLoader<UPCGExAssetCollection>> CollectionsLoader;

	TObjectPtr<UPCGExAssetCollection> MainCollection;
	bool bPickMaterials = false;

	/** CollectionMap source mode: rebuilt from the upstream Collection Map pin. Kept alive by its own asset handles. */
	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;

	/** CollectionMap source mode: unique sub-collections reachable from unpacked entries, keyed by CollectionGUID.
	 * Feeds the mapped FCollectionSource; the referenced assets are hard refs of the unpacked collections. */
	TMap<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>> SubCollectionSources;

	/** CollectionMap source mode: H64(HostGUID, RawEntryIndex) -> sub-collection GUID. Built once in Boot;
	 * per-point routing keys are pure lookups against this. */
	TMap<uint64, PCGExValueHash> EntryToSubCollectionKey;

	const UPCGExSelectorFactoryData* SelectorFactory = nullptr;

	TSharedPtr<PCGExCollections::FPickPacker> CollectionPickDatasetPacker;
	TSharedPtr<PCGExCollections::FSelectorSharedDataCache> SelectorSharedDataCache;

	FPCGExSocketOutputDetails OutputSocketDetails;
	TSharedPtr<PCGExData::FPointIOCollection> SocketsCollection;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExAssetStagingElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AssetStaging)

	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
	
	// CollectionMap source mode unpacks (and blocking-loads) the upstream map in Boot; the loader's
	// miss path marshals to the game thread and requires game-thread affinity during PrepareData
	// (see LoadAndCacheBlockingSet in PCGExStreamingHelpers.cpp). Gated on the mode so Default-mode
	// nodes keep off-thread preparation -- same pattern as FPCGExStagingFittingElement.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	
};

namespace PCGExAssetStaging
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAssetStagingContext, UPCGExAssetStagingSettings>
	{
	protected:
		// The range loop runs twice with different bodies: an optional selector pre-resolve
		// stage before the main points loop, then the material-writing stage after it.
		enum class ERangeStage : uint8
		{
			PreResolve = 0,
			Materials  = 1,
		};

		ERangeStage RangeStage = ERangeStage::Materials;
		bool bFiltersPrimed = false;

		int32 NumPoints = 0;
		int32 NumInvalid = 0;

		bool bInherit = false;
		// Effective pruning: settings toggle, forced off in CollectionMap source mode where
		// nothing ever fails (unresolvable points pass through untouched).
		bool bPruneEnabled = false;
		bool bOutputWeight = false;
		bool bOneMinusWeight = false;
		bool bNormalizedWeight = false;
		bool bUsesDensity = false;

		TArray<int8> Mask;

		bool bApplyFitting = true;
		FPCGExFittingDetailsHandler FittingHandler;
		FPCGExFittingVariationsDetails Variations;

		// CollectionMap source mode: per-point routing keys (sub-collection GUID, 0 = pass through).
		TSharedPtr<TArray<PCGExValueHash>> SourceKeys;
		TSharedPtr<PCGExCollections::FCollectionSource> Source;
		TSharedPtr<PCGExCollections::FSocketHelper> SocketHelper;

		// Micro-cache redistribution: unique staged hashes resolve once into MicroTargets in
		// Process(); MicroTargetIndex routes each point to its target (-1 = untouched). One
		// micro helper serves all collections. Non-null MicroRedistributeHelper is the mode
		// switch for the point loop.
		struct FMicroRefreshTarget
		{
			const FPCGExAssetCollectionEntry* Entry = nullptr;
			// Shared ref so a collection cache rebuild can't free the cache mid-generation.
			TSharedPtr<const PCGExAssetCollection::FMicroCache> MicroCache;
			uint32 HostGUID = 0;
			uint16 RawEntryIndex = 0;
			int32 HighestSlotIndex = -1;
		};

		TArray<FMicroRefreshTarget> MicroTargets;
		TArray<int32> MicroTargetIndex;
		TSharedPtr<PCGExCollections::FMicroSelectorHelper> MicroRedistributeHelper;

		TSharedPtr<PCGExData::TBuffer<int32>> WeightWriter;
		TSharedPtr<PCGExData::TBuffer<double>> NormalizedWeightWriter;
		TSharedPtr<PCGExData::TBuffer<FName>> EntryTypeWriter;
		TSharedPtr<PCGExData::TBuffer<FVector>> TranslationWriter;

		TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> PathWriter;

		// Material handling 
		TSharedPtr<PCGExMT::TScopedNumericValue<int8>> HighestSlotIndex;
		TArray<TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>>> MaterialWriters; // Per valid slot writers

		TArray<const FPCGExAssetCollectionEntry*> CachedPicks;
		TArray<int8> MaterialPick;

		TSharedPtr<PCGExData::TBuffer<int64>> HashWriter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
		virtual void Write() override;

	protected:
		/**
		 * Pre-resolve pass body. bCommit=false: parallel per-scope filter + first choices.
		 * bCommit=true: sequential whole-range commit -- MUST be called with a single scope
		 * covering all points, in point-index order (that ordering is the claim priority).
		 */
		void PreResolveScope(const PCGExMT::FScope& Scope, bool bCommit);

		/** Micro-cache redistribution loop body: keeps staged entry picks, re-picks secondary indices only. */
		void ProcessPointsMicroRedistribute(const PCGExMT::FScope& Scope);
	};
}
