// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"

#include "Core/PCGExPointsProcessor.h"


#include "PCGExSplitPath.generated.h"

namespace PCGExSplitPath
{
	const FName SourceSplitFilters = TEXT("Split Conditions");
}

UENUM()
enum class EPCGExPathSplitAction : uint8
{
	Split      = 0 UMETA(DisplayName = "Split", ToolTip="Duplicate the split point so the original becomes a new end, and the copy a new start."),
	Remove     = 1 UMETA(DisplayName = "Remove", ToolTip="Remove the split point, shrinking both the previous and next paths."),
	Disconnect = 2 UMETA(DisplayName = "Disconnect", ToolTip="Disconnect the split point from the next one, starting a new path from the next."),
	Partition  = 3 UMETA(DisplayName = "Partition", ToolTip="Works like split but only create new data set as soon as the filter result changes from its previous result."),
	Switch     = 4 UMETA(DisplayName = "Switch", ToolTip="Use the result of the filter as a switch signal to change between keep/prune behavior."),
};

UENUM()
enum class EPCGExPathSplitInitialValue : uint8
{
	Constant          = 0 UMETA(DisplayName = "Constant", ToolTip="Use a constant value."),
	ConstantPreserve  = 1 UMETA(DisplayName = "Constant (Preserve)", ToolTip="Use a constant value, but does not switch if the first value is the same."),
	FromPoint         = 2 UMETA(DisplayName = "From Point", ToolTip="Use the first point starting value."),
	FromPointPreserve = 3 UMETA(DisplayName = "From Point (Preserve)", ToolTip="Use the first point starting value, but preserve its behavior."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/generate/path-split"))
class UPCGExSplitPathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	// TODO : Display split mode in title
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathSplit, "Path : Split", "Split existing paths into multiple new paths.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExSplitPath::SourceSplitFilters, "Filters used to know if a point should be split", PCGExFactories::PointFilters(), true)
	//~End UPCGExPointsProcessorSettings

	/** If both split and remove are true, the selected behavior takes priority */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPathSplitAction SplitAction = EPCGExPathSplitAction::Split;

	/** How the initial value is determined for Switch/Partition modes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SplitAction == EPCGExPathSplitAction::Switch || SplitAction == EPCGExPathSplitAction::Partition", EditConditionHides))
	EPCGExPathSplitInitialValue InitialBehavior = EPCGExPathSplitInitialValue::Constant;

	/** The initial switch value to start from. If false, will only starting to create paths after the first true result. If false, will start to create paths from the beginning and stop at the first true result instead.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InitialBehavior == EPCGExPathSplitInitialValue::Constant && (SplitAction == EPCGExPathSplitAction::Switch || SplitAction == EPCGExPathSplitAction::Partition)", EditConditionHides))
	bool bInitialValue = false;

	/** Should point insertion be inclusive of the behavior change */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SplitAction == EPCGExPathSplitAction::Switch || SplitAction == EPCGExPathSplitAction::Partition", EditConditionHides))
	bool bInclusive = false;

	/** Whether to output single-point data or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitSinglePointOutputs = true;

	/** Tag even-numbered split results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfEvenSplit = true;

	/** Tag to apply to even-numbered split paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfEvenSplit"))
	FString IsEvenTag = TEXT("EvenSplit");

	/** Tag odd-numbered split results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfOddSplit = false;

	/** Tag to apply to odd-numbered split paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfOddSplit"))
	FString IsOddTag = TEXT("OddSplit");
};

struct FPCGExSplitPathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExSplitPathElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSplitPathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SplitPath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSplitPath
{
	struct PCGEXELEMENTSPATHS_API FSubPath
	{
		bool bEven = false;
		int32 Start = -1;
		int32 End = -1;
		int32 Count = 0;

		FSubPath()
		{
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSplitPathContext, UPCGExSplitPathSettings>
	{
		bool bClosedLoop = false;

		TArray<FSubPath> SubPaths;
		TArray<TSharedPtr<PCGExData::FPointIO>> SubPathsIOs;

		bool bWrapLastPath = false;
		bool bAddOpenTag = false;
		int8 bLastResult = false;
		bool bEven = true;

		int32 LastIndex = -1;
		int32 CurrentSubPath = -1;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		void DoActionSplit(const int32 Index);
		void DoActionRemove(const int32 Index);
		void DoActionDisconnect(const int32 Index);
		void DoActionPartition(const int32 Index);
		void DoActionSwitch(const int32 Index);

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
		virtual void Output() override;
	};
}
