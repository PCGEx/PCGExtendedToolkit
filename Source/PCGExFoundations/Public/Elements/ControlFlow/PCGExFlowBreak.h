// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExVersion.h"
#include "Core/PCGExPointsProcessor.h"

#include "PCGExFlowBreak.generated.h"

UENUM()
enum class EPCGExRecursionTrackerType : uint8
{
	Simple = 0 UMETA(DisplayName = "Simple", ToolTip="Simple recursion tracker. Can update multiple trackers at once.", ActionIcon="PCGEx.Pin.OUT_RecursionTracker"),
	Branch = 1 UMETA(DisplayName = "Branch", ToolTip="Branch recusion tracker. Can only work with a single tracker", ActionIcon="PCGEx.Pin.OUT_RecursionTracker", SearchHints = "Branch")
};

UENUM()
enum class EPCGExRecursionTrackerMode : uint8
{
	Create         = 0 UMETA(DisplayName = "Create", Tooltip="Create a new tracker. This is for creating an initial tracker outside the subgraph."),
	Update         = 1 UMETA(DisplayName = "Update", Tooltip="Process and update an existing tracker. This is for use inside the recursive subgraph."),
	CreateOrUpdate = 2 UMETA(DisplayName = "Create or Update", Tooltip="Create a new tracker if input is empty, otherwise fallback to mutate. Useful to create tracker directly inside the recursive subgraph."),
};

namespace PCGExRecursionTracker
{
	const FName SourceTrackerLabel = FName("Tracker");
	const FName OutputTrackerLabel = FName("Tracker");
	const FName OutputContinueLabel = FName("Continue");
	const FName OutputStopLabel = FName("Stop");

	const FName SourceTrackerFilters = FName("Tracker Filters");
	const FName SourceTestData = FName("Test Data");
	const FName OutputProgressLabel = FName("Progress");
	const FName OutputIndexLabel = FName("Index");
	const FName OutputRemainderLabel = FName("Remainder");
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="utilities/control-flow/break"))
class UPCGExRecursionTrackerSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExFlowBreakElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Break, "Break", "A Simple Recursion tracker to make working with recursive subgraphs easier. Acts as a \"break\" by tracking a counter, and/or checking if data meet certain requirements.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::ControlFlow;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Constant);
	}

	virtual bool GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip) const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

	virtual bool HasDynamicPins() const override;
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

#if PCGEX_ENGINE_VERSION < 507
	virtual EPCGDataType GetCurrentPinTypes(const UPCGPin* InPin) const override;
#else
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	/** How is this recursion tracker supposed to be used. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExRecursionTrackerType Type = EPCGExRecursionTrackerType::Branch;

	/** How is this recursion tracker supposed to be used. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExRecursionTrackerMode Mode = EPCGExRecursionTrackerMode::CreateOrUpdate;

	/** Name of the bool attribute that will be set on the tracker. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Update", EditConditionHides, ClampMin=0))
	FName ContinueAttributeName = "Continue";

	/** Max count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Update", EditConditionHides, ClampMin=0))
	int32 MaxCount = 20;

	/** Tags to be added to the tracker */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	FString AddTags = TEXT("");

	/** Tags to be removed from the tracker(s) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides))
	FString RemoveTags = TEXT("");

	/** Value to add to the counter each update. Negative values decrement. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides), AdvancedDisplay)
	int32 CounterUpdate = -1;

	/** If enabled, will create a pin that outputs the normalized progress value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra Outputs", meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides))
	bool bOutputProgress = false;

	/** If enabled, will create a pin that outputs the current iteration index (Max - Remainder). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra Outputs", meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides))
	bool bOutputIndex = false;

	/** If enabled, will create a pin that outputs the current remainder. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra Outputs", meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides))
	bool bOutputRemainder = false;

	/** Output (1 - Progress) instead of Progress. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ One Minus", EditCondition="Mode != EPCGExRecursionTrackerMode::Create && bOutputProgress", EditConditionHides))
	bool bOneMinus = false;

	/** If enabled, will override the value of the "Continue" attribute to a valid one. Use this is you give the tracker some attribute set that may already have a boolean with the same name and a wrong value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides), AdvancedDisplay)
	bool bForceOutputContinue = false;

	/** If enabled, does additional collection-level filtering on a separate set of datas. If no data passes those filters, the tracker will return a single false value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create && Type == EPCGExRecursionTrackerType::Simple", EditConditionHides))
	bool bDoAdditionalDataTesting = false;

	/** Add metadata entry when creating a tracker from existing attribute set data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode != EPCGExRecursionTrackerMode::Create", EditConditionHides), AdvancedDisplay)
	bool bAddEntryWhenCreatingFromExistingData = false;

	/** An offset applied when creating a tracker in "Create or Update" mode. The default value assume the tracker is created from inside a subgraph and thus that one iteration passed already. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode == EPCGExRecursionTrackerMode::CreateOrUpdate", EditConditionHides), AdvancedDisplay)
	int32 RemainderOffsetWhenCreateInsteadOfUpdate = -1;

	/** For OCD purposes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bGroupBranchPins = false;
};

class FPCGExFlowBreakElement final : public IPCGExElement
{
public:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT

protected:
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}
};
