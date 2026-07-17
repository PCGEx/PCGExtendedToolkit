// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExFactories.h"
#include "Math/PCGExMathMean.h"
#include "Math/OBB/PCGExOBB.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExSelfPruning.generated.h"

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExSelfPruningMode : uint8
{
	Prune       = 0 UMETA(DisplayName = "Prune", ToolTip="Prune points"),
	WriteResult = 1 UMETA(DisplayName = "Write Result", ToolTip="Write the number of overlaps"),
};

UENUM()
enum class EPCGExSelfPruningExpandOrder : uint8
{
	None   = 0 UMETA(DisplayName = "None", ToolTip="Do not expand bounds"),
	Before = 1 UMETA(DisplayName = "Before Transform", ToolTip="Expand bounds before world transform"),
	After  = 2 UMETA(DisplayName = "After Transform", ToolTip="Expand bounds after world transform"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="sampling/self-pruning"))
class UPCGExSelfPruningSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(SelfPruning, "Self Pruning", "A slower, more precise self pruning node.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Filter);
	}
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters which points can be processed as overlapping", PCGExFactories::PointFilters(), false)

protected:
	virtual bool HasDynamicPins() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Whether to prune points or write the number of overlaps */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExSelfPruningMode Mode = EPCGExSelfPruningMode::Prune;

	/** Whether to sort hash components or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Sort Direction"))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	/** Sort over a random per-point value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Randomize", EditCondition="Mode == EPCGExSelfPruningMode::Prune", EditConditionHides))
	bool bRandomize = true;

	/** Sort over a random per-point value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="  └─ Range", EditCondition="bRandomize && Mode == EPCGExSelfPruningMode::Prune", EditConditionHides, ClampMin=0, ClampMax=1))
	double RandomRange = 0.05;

	/** Name of the attribute to write the number of overlap to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Output to", EditCondition="Mode == EPCGExSelfPruningMode::WriteResult", EditConditionHides))
	FName NumOverlapAttributeName = FName("NumOverlaps");

	/** Discrete mode write the number as-is, relative will normalize against the highest number of overlaps found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Units", EditCondition="Mode == EPCGExSelfPruningMode::WriteResult", EditConditionHides))
	EPCGExMeanMeasure Units = EPCGExMeanMeasure::Discrete;

	/** Whether to do a OneMinus on the normalized overlap count value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="   └─ OneMinus", EditCondition="Mode == EPCGExSelfPruningMode::WriteResult && Units == EPCGExMeanMeasure::Relative", EditConditionHides))
	bool bOutputOneMinusOverlap = false;

	/** If enabled, output the pruned-away points to a separate 'Discarded' pin. Authoring-time only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Output Discarded", EditCondition="Mode == EPCGExSelfPruningMode::Prune", EditConditionHides))
	bool bOutputDiscard = false;

	/** If enabled, does very precise and EXPENSIVE spatial tests. Only supported for pruning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta=(PCG_NotOverridable))
	bool bPreciseTest = false;

	/** If and how to expand the primary bounds (bounds used for the main point being evaluated) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta=(PCG_NotOverridable))
	EPCGExSelfPruningExpandOrder PrimaryMode = EPCGExSelfPruningExpandOrder::None;

	/** Primary Expansion value. Uniform, discrete offset applied to bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta=(PCG_Overridable, DisplayName=" └─ Primary Expansion", EditCondition="PrimaryMode != EPCGExSelfPruningExpandOrder::None", EditConditionHides))
	FPCGExInputShorthandSelectorDouble PrimaryExpansionValue = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0, false);

	/** If and how to expand the primary bounds (bounds used for neighbors points against the main point being evaluated) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta=(PCG_NotOverridable))
	EPCGExSelfPruningExpandOrder SecondaryMode = EPCGExSelfPruningExpandOrder::None;

	/** Secondary Expansion value. Uniform, discrete offset applied to bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta=(PCG_Overridable, DisplayName=" └─ Secondary Expansion", EditCondition="SecondaryMode != EPCGExSelfPruningExpandOrder::None", EditConditionHides))
	FPCGExInputShorthandSelectorDouble SecondaryExpansionValue = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType PrimaryExpansionInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector PrimaryExpansionAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double PrimaryExpansion_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType SecondaryExpansionInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector SecondaryExpansionAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double SecondaryExpansion_DEPRECATED = 0;

#pragma endregion
};

struct FPCGExSelfPruningContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSelfPruningElement;

	TSharedPtr<PCGExData::FPointIOCollection> Discarded;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSelfPruningElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SelfPruning)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSelfPruning
{
	struct FCandidateInfos
	{
		FCandidateInfos() = default;

		int32 Index = -1;
		int32 Overlaps = 0;
		int8 bSkip = 0;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSelfPruningContext, UPCGExSelfPruningSettings>
	{
	protected:
		TSharedPtr<PCGExDetails::TSettingValue<double>> PrimaryExpansion;
		TSharedPtr<PCGExDetails::TSettingValue<double>> SecondaryExpansion;

		TBitArray<> Mask;
		TArray<int32> Priority;
		TArray<FCandidateInfos> Candidates;
		TArray<FBox> BoxSecondary;

		// Pre-built OBBs for precise testing (only allocated when bPreciseTest is true)
		TArray<PCGExMath::OBB::FOBB> PrimaryOBBs;
		TArray<PCGExMath::OBB::FOBB> SecondaryOBBs;

		int32 LastCandidatesCount = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		virtual void CompleteWork() override;
	};
}
