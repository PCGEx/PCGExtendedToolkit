// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointsProcessor.h"
#include "Math/PCGExMathMean.h"

#include "PCGExUberFilterCollections.generated.h"

class UPCGExPickerFactoryData;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="filters/uber-filter-data"))
class UPCGExUberFilterCollectionsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
#if WITH_EDITOR

public:
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(UberFilterCollections, "Uber Filter (Data)", "Filter entire collections based on multiple rules & conditions.");

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

	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

	virtual bool HasDynamicPins() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override;
	virtual bool GetIsMainTransactional() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), true)
	//~End UPCGExPointsProcessorSettings

	/** Write result to point instead of split outputs */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExUberFilterCollectionsMode Mode = EPCGExUberFilterCollectionsMode::All;

	/** Partial value type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExUberFilterCollectionsMode::Partial", EditConditionHides))
	EPCGExMeanMeasure Measure = EPCGExMeanMeasure::Relative;

	/** Partial value comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExUberFilterCollectionsMode::Partial", EditConditionHides))
	EPCGExComparison Comparison = EPCGExComparison::EqualOrGreater;

	/** Partial value type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExUberFilterCollectionsMode::Partial && Measure == EPCGExMeanMeasure::Relative", EditConditionHides, ClampMin=0, ClampMax=1))
	double DblThreshold = 0.5;

	/** Partial value type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExUberFilterCollectionsMode::Partial && Measure == EPCGExMeanMeasure::Discrete", EditConditionHides, ClampMin=0))
	int32 IntThreshold = 10;

	/** Rounding mode for relative measures */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Invert the filter result */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSwap = false;

private:
	friend class FPCGExUberFilterCollectionsElement;
};

struct FPCGExUberFilterCollectionsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExUberFilterCollectionsElement;

	bool bHasOnlyCollectionFilters = false;

	TArray<TObjectPtr<const UPCGExPickerFactoryData>> PickerFactories;

	TSharedPtr<PCGExData::FPointIOCollection> Inside;
	TSharedPtr<PCGExData::FPointIOCollection> Outside;

	int32 NumPairs = 0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExUberFilterCollectionsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(UberFilterCollections)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExUberFilterCollections
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExUberFilterCollectionsContext, UPCGExUberFilterCollectionsSettings>
	{
		int32 NumPoints = 0;
		int32 NumInside = 0;
		int32 NumOutside = 0;

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
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void Output() override;
	};
}
