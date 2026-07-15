// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Sorting/PCGExSortingDetails.h"

#include "PCGExMergePoints.generated.h"

class FPCGExPointIOMerger;

namespace PCGExMatching
{
	class FDataMatcher;
}

struct PCGEXFOUNDATIONS_API FPCGExMergeList
{
	TArray<TSharedPtr<PCGExData::FPointIO>> IOs;
	TSharedPtr<FPCGExPointIOMerger> Merger;
	TSharedPtr<PCGExData::FFacade> CompositeDataFacade;

	FPCGExMergeList() = default;

	void Merge(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const FPCGExCarryOverDetails* InCarryOverDetails, const FPCGExNameFiltersDetails* InTagsToAttributes = nullptr);
	void Write(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/merge-points"))
class UPCGExMergePointsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExMergePointsSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(MergePoints, "Merge Points", "Merge point collections, optionally grouping them using matching rules.");

	virtual TArray<FText> GetNodeTitleAliases() const override;
	
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Misc);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Matching settings to determine which data should be grouped together. When disabled, all inputs merge into one output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExMatchingDetails MatchingDetails = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Default);

	/** If enabled, each data can only belong to one group (first match). If disabled, data can appear in multiple groups. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable), AdvancedDisplay)
	bool bExclusivePartitions = true;

	/** Controls the order in which data will be sorted if sorting rules are used */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	/** Meta filter settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

	/** If enabled, will convert tags into per-point attributes so their semantics survive the merge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bTagToAttributes = false;

	/** Tags to convert to attributes: simple tags become boolean (presence), tag:value pairs become their typed value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bTagToAttributes"))
	FPCGExNameFiltersDetails TagsToAttributes = FPCGExNameFiltersDetails(false);
};

struct FPCGExMergePointsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExMergePointsElement;

	FPCGExMatchingDetails MatchingDetails;
	FPCGExCarryOverDetails CarryOverDetails;

	bool bTagToAttributes = false;
	FPCGExNameFiltersDetails TagsToAttributes;

	TSharedPtr<PCGExMatching::FDataMatcher> DataMatcher;
	TArray<TArray<int32>> Partitions;
	TArray<TSharedPtr<FPCGExMergeList>> MergeLists;
	TArray<int32> UnmatchedIndices;
};

class FPCGExMergePointsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(MergePoints)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
