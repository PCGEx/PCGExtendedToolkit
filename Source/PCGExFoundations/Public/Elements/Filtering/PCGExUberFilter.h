// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExFilterDetails.h"

#include "PCGExUberFilter.generated.h"

class UPCGExPickerFactoryData;

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

UENUM()
enum class EPCGExUberFilterMode : uint8
{
	Partition = 0 UMETA(DisplayName = "Partition points", ToolTip="Create inside/outside dataset from the filter results."),
	Write     = 1 UMETA(DisplayName = "Write result", ToolTip="Simply write filter result to an attribute but doesn't change point structure."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="filters/uber-filter"))
class UPCGExUberFilterSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
#if WITH_EDITOR

public:
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(UberFilter, "Uber Filter", "Filter points based on multiple rules & conditions.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterHub);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual bool OutputPinsCanBeDeactivated() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return Mode == EPCGExUberFilterMode::Write;
	}

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	virtual FName GetMainOutputPin() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), true)
	//~End UPCGExPointsProcessorSettings

	/** Write result to point instead of split outputs */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExUberFilterMode Mode = EPCGExUberFilterMode::Partition;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Result", PCG_Overridable, EditCondition="Mode == EPCGExUberFilterMode::Write", EditConditionHides))
	FPCGExFilterResultDetails ResultDetails;

#pragma region DEPRECATED

	/** Name of the attribute to write result to */
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ResultAttributeName_DEPRECATED = NAME_None;

#pragma endregion

	/** Invert the filter result */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSwap = false;

	/** If enabled, will output discarded elements, otherwise omit creating the data entirely. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_Overridable))
	bool bOutputDiscardedElements = true;

	/** Add a tag if at least one point passed the filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfAnyPointPassed = false;

	/** Tag to add when at least one point passes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfAnyPointPassed"))
	FString HasAnyPointPassedTag = TEXT("SomePointsPassed");

	/** Add a tag if all points passed the filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfAllPointsPassed = false;

	/** Tag to add when all points pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfAllPointsPassed"))
	FString AllPointsPassedTag = TEXT("AllPointsPassed");

	/** Add a tag if no points passed the filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfNoPointPassed = false;

	/** Tag to add when no points pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfNoPointPassed"))
	FString NoPointPassedTag = TEXT("NoPointPassed");

	/** How should point that aren't picked be considered? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback UnpickedFallback = EPCGExFilterFallback::Fail;

private:
	friend class FPCGExUberFilterElement;
};

struct FPCGExUberFilterContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExUberFilterElement;

	TArray<TObjectPtr<const UPCGExPickerFactoryData>> PickerFactories;

	TSharedPtr<PCGExData::FPointIOCollection> Inside;
	TSharedPtr<PCGExData::FPointIOCollection> Outside;

	int32 NumPairs = 0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExUberFilterElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(UberFilter)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExUberFilter
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExUberFilterContext, UPCGExUberFilterSettings>
	{
		int32 NumInside = 0;
		int32 NumOutside = 0;

		TSharedPtr<PCGExMT::TScopedArray<int32>> IndicesInside;
		TSharedPtr<PCGExMT::TScopedArray<int32>> IndicesOutside;

		FPCGExFilterResultDetails Results = FPCGExFilterResultDetails(false, false);

		bool bUsePicks = false;
		TSet<int32> Picks;

	public:
		TSharedPtr<PCGExData::FPointIO> Inside;
		TSharedPtr<PCGExData::FPointIO> Outside;

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		TSharedPtr<PCGExData::FPointIO> CreateIO(const TSharedRef<PCGExData::FPointIOCollection>& InCollection, const PCGExData::EIOInit InitMode) const;

		virtual void OnPointsProcessingComplete() override;
	};
}
