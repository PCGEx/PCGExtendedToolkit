// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "PCGExProperty.h"
#include "Core/PCGExPointsProcessor.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/PCGExMath.h"

#include "PCGExDistributeTuple.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "tuple distribute weighted random", PCGExNodeLibraryDoc="metadata/keys/tuple-distribute"))
class UPCGExDistributeTupleSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(DistributeTuple, "Tuple : Distribute", "Distribute weighted tuple row values across input points.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/** Tuple composition - defines the columns (property types and names) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ToolTip="Tuple composition defines the columns (property types and names)."))
	FPCGExPropertySchemaCollection Composition;

	/** Weighted tuple values - each row has a weight and per-column overrides */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ToolTip="Weighted tuple values. Toggle 'Enabled' per column to include/exclude values. Rows auto-sync with composition changes.", FullyExpand=true))
	TArray<FPCGExWeightedPropertyOverrides> Values;

	/** How to distribute rows across points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistribution Distribution = EPCGExDistribution::WeightedRandom;

	/** Index safety mode when Distribution is Index and point count exceeds row count */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Distribution == EPCGExDistribution::Index", EditConditionHides))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Tile;

	/** Which components contribute to seed generation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExSeedComponents", EditCondition="Distribution != EPCGExDistribution::Index", EditConditionHides))
	uint8 SeedComponents = static_cast<uint8>(EPCGExSeedComponents::Local | EPCGExSeedComponents::Settings);

	/** Local seed offset */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Distribution != EPCGExDistribution::Index", EditConditionHides))
	int32 LocalSeed = 0;

	/** Whether to output the picked row index as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputRowIndex = false;

	/** Name of the attribute to write the picked row index to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bOutputRowIndex"))
	FName RowIndexAttributeName = "TupleRowIndex";

	/** Whether to output the picked row's weight as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputWeight = false;

	/** Name of the attribute to write the picked row's weight to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bOutputWeight"))
	FName WeightAttributeName = "TupleWeight";

	/** Emit the "Map" attribute set describing external resources referenced by written values (e.g. Collection Entry picks). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", AdvancedDisplay, meta=(DisplayName="Output Map"))
	bool bOutputMap = false;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

public:
};

struct FPCGExDistributeTupleContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExDistributeTupleElement;

	// Source properties declaring a sidecar pin, unioned across processors (lock: processors finish in
	// parallel). Flushed once after StageOutputs when bOutputMap.
	FCriticalSection SidecarLock;
	TArray<const FPCGExProperty*> SidecarSources;

	/** Output dependencies of every authored value (e.g. Collection Entry picks). */
	virtual void RegisterAssetDependencies() override;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExDistributeTupleElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(DistributeTuple)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExDistributeTuple
{
	/** Per-column compiled output data */
	struct FColumnOutput
	{
		/** Deep copy of the schema property that owns the output buffer */
		FInstancedStruct OwnedProperty;

		/** Cached raw pointer to the property (resolved once during init) */
		const FPCGExProperty* WriterPtr = nullptr;

		/** Per-row source properties (nullptr if that column is disabled in a given row) */
		TArray<const FPCGExProperty*> RowSources;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExDistributeTupleContext, UPCGExDistributeTupleSettings>
	{
		int32 NumRows = 0;

		/** Cumulative weight array for weighted random distribution */
		TArray<int32> CumulativeWeights;
		int32 TotalWeight = 0;

		/** Per-column output data */
		TArray<FColumnOutput> Columns;

		/** Optional writers */
		TSharedPtr<PCGExData::TBuffer<int32>> RowIndexWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> WeightWriter;

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
	};
}
