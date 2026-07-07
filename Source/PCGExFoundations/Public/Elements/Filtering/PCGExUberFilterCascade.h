// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactories.h"

#include "PCGExFilterCommon.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Elements/Filtering/PCGExUberFilter.h"

#include "PCGExUberFilterCascade.generated.h"

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

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="filters/uber-filter-cascade"))
class UPCGExUberFilterCascadeSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(UberFilterCascade, "Uber Filter (Cascade)", "Filter points into multiple buckets based on ordered filter groups. First matching group claims the point.");

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
		return Mode != EPCGExUberFilterMode::Write;
	}

	virtual bool HasDynamicPins() const override;
	
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
	//~End UPCGExPointsProcessorSettings

	/** Number of filter groups (branches) to evaluate. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1))
	int32 NumBranches = 3;

	/** How results are output. Partition splits points into per-branch pins; Write records the matched branch index onto a single output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExUberFilterMode Mode = EPCGExUberFilterMode::Partition;

	/** Name of the int32 attribute the matched branch index is written to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Partition Attribute", EditCondition="Mode == EPCGExUberFilterMode::Write", EditConditionHides))
	FName PartitionAttributeName = FName("Partition");

	/** Base value written to the attribute. Unmatched points receive this value; a point matching branch i receives DefaultValue + i + 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="Mode == EPCGExUberFilterMode::Write", EditConditionHides))
	int32 DefaultValue = 0;

	UPROPERTY(meta=(PCG_NotOverridable))
	TArray<FName> InputLabels = {TEXT("→ 0"), TEXT("→ 1"), TEXT("→ 2")};

	UPROPERTY(meta=(PCG_NotOverridable))
	TArray<FName> OutputLabels = {TEXT("0 →"), TEXT("1 →"), TEXT("2 →")};

	/** If enabled, will output unmatched points to the Outside pin, otherwise omit creating the data entirely. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExUberFilterMode::Partition", EditConditionHides))
	bool bOutputDiscardedElements = true;

private:
	friend class FPCGExUberFilterCascadeElement;
};

struct FPCGExUberFilterCascadeContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExUberFilterCascadeElement;

	TArray<TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>> BranchFilterFactories;
	TArray<TSharedPtr<PCGExData::FPointIOCollection>> BranchOutputs;
	TSharedPtr<PCGExData::FPointIOCollection> DefaultOutput;

	int32 NumPairs = 0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExUberFilterCascadeElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(UberFilterCascade)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExUberFilterCascade
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExUberFilterCascadeContext, UPCGExUberFilterCascadeSettings>
	{
		TArray<TSharedPtr<PCGExPointFilter::FManager>> BranchManagers;
		TArray<TSharedPtr<PCGExMT::TScopedArray<int32>>> BranchIndices;
		TArray<int32> BranchCounts;

		TSharedPtr<PCGExData::TBuffer<int32>> PartitionBuffer;

	public:
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
