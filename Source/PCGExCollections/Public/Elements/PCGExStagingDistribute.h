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

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage prepare spawn proxy", PCGExNodeLibraryDoc="staging/staging-distribute"))
class UPCGExAssetStagingSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	
	UPCGExAssetStagingSettings();
	
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	
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

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get staged.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExCollectionSource CollectionSource = EPCGExCollectionSource::Asset;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollectionSource == EPCGExCollectionSource::Asset", EditConditionHides))
	TSoftObjectPtr<UPCGExAssetCollection> AssetCollection;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollectionSource == EPCGExCollectionSource::AttributeSet", EditConditionHides))
	FPCGExRoamingAssetCollectionDetails AttributeSetDetails;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Attribute", EditCondition="CollectionSource == EPCGExCollectionSource::Attribute", EditConditionHides))
	FName CollectionPathAttributeName = "CollectionPath";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="CollectionSource != EPCGExCollectionSource::AttributeSet", EditConditionHides))
	EPCGExStagingOutputMode OutputMode = EPCGExStagingOutputMode::CollectionMap;

	/** The name of the attribute to write asset path to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode == EPCGExStagingOutputMode::Attributes"))
	FName AssetPathAttributeName = "AssetPath";

	//** If enabled, doesn't go through collections recursively and assign top-level collections "as assets" */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
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
	 * Note : LEGACY Nodes only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Distribution (Entry)", EditCondition="SelectorMode == EPCGExSelectorMode::Legacy", EditConditionHides))
	FPCGExMicroCacheDistributionDetails EntryDistributionSettings;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bApplyFitting = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bApplyFitting", EditConditionHides))
	FPCGExScaleToFitDetails ScaleToFit;

	/** When enabled, entries that define a Scale to Fit override (entry-local or collection-global) replace this node's Scale to Fit for their points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Entry Overrides", EditCondition="bApplyFitting", EditConditionHides))
	bool bConsiderEntryScaleToFit = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bApplyFitting", EditConditionHides))
	FPCGExJustificationDetails Justification;

	/** When enabled, entries that define a Justification override (entry-local or collection-global) replace this node's Justification for their points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Entry Overrides", EditCondition="bApplyFitting", EditConditionHides))
	bool bConsiderEntryJustification = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="bApplyFitting", EditConditionHides))
	FPCGExFittingVariationsDetails Variations;


	//** If enabled, filter output based on whether a staging has been applied or not (empty entry).  Current implementation is slow. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bPruneEmptyPoints = true;

	/** Filter output based on the type of collection entry. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bDoFilterEntryType = false;

	/** Lets you filter which collection type gets staged.
	 * This is most useful when using per-point collections and you want to stage only certain types of assets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, EditConditions="bDoFilterEntryType"))
	FPCGExStagedTypeFilterDetails EntryTypeFilter;

	/** Write the collection entry type (Mesh, Actor, etc.) to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteEntryType = false;

	/** Name of the FName entry type will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bWriteEntryType"))
	FName EntryTypeAttributeName = FName("EntryType");

	/** Update point scale so staged asset fits within its bounds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable))
	EPCGExWeightOutputMode WeightToAttribute = EPCGExWeightOutputMode::NoOutput;

	/** The name of the attribute to write asset weight to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="WeightToAttribute != EPCGExWeightOutputMode::NoOutput && WeightToAttribute != EPCGExWeightOutputMode::NormalizedToDensity && WeightToAttribute != EPCGExWeightOutputMode::NormalizedInvertedToDensity"))
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

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExAssetStaging
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAssetStagingContext, UPCGExAssetStagingSettings>
	{
	protected:
		int32 NumPoints = 0;
		int32 NumInvalid = 0;

		bool bInherit = false;
		bool bOutputWeight = false;
		bool bOneMinusWeight = false;
		bool bNormalizedWeight = false;
		bool bUsesDensity = false;

		TArray<int8> Mask;

		bool bApplyFitting = true;
		FPCGExFittingDetailsHandler FittingHandler;
		FPCGExFittingVariationsDetails Variations;

		TSharedPtr<TArray<PCGExValueHash>> SourceKeys;
		TSharedPtr<PCGExCollections::FCollectionSource> Source;
		TSharedPtr<PCGExCollections::FSocketHelper> SocketHelper;

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
	};
}
