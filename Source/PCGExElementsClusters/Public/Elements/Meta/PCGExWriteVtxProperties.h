// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExWriteVtxProperties.generated.h"

#define PCGEX_FOREACH_FIELD_VTXEXTRAS(MACRO) \
MACRO(VtxNormal, FVector, FVector::OneVector) \
MACRO(VtxEdgeCount, int32, 0)

class FPCGExVtxPropertyOperation;
class UPCGExVtxPropertyFactoryData;

namespace PCGExWriteVtxProperties
{
	class FBatch;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-vtx-properties"))
class UPCGExWriteVtxPropertiesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(WriteVtxProperties, "Cluster : Vtx Properties", "Extract & write extra informations from the edges connected to the vtx.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(NeighborSampler);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	/** Mutate Vtx into their OOB based on neighboring connections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable))
	bool bMutateVtxToOOB = false;

	/** If enabled, use precise min box fit. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Precise Fit", EditCondition="bMutateVtxToOOB", EditConditionHides))
	bool bUseMinBoxFit = true;

	/** Which fitted axis maps onto X, Y and Z. Also decides which axis Normal reads. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_NotOverridable, DisplayName=" └─ Axis Order", EditCondition="bMutateVtxToOOB || bWriteVtxNormal", EditConditionHides))
	EPCGExAxisOrder AxisOrder = EPCGExAxisOrder::XYZ;

	/** Write each vertex's edge count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteVtxEdgeCount = false;

	/** Name of the 'int32' vertex attribute to write the edge count to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="EdgeCount", PCG_Overridable, EditCondition="bWriteVtxEdgeCount"))
	FName VtxEdgeCountAttributeName = FName("EdgeCount");

	/** Write normal from edges on vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteVtxNormal = false;

	/** Name of the 'normal' vertex attribute to write normal to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Normal", PCG_Overridable, EditCondition="bWriteVtxNormal"))
	FName VtxNormalAttributeName = FName("Normal");

	/** Which axis of the vtx OOB to use as normal.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName=" └─ Axis", PCG_Overridable, EditCondition="bWriteVtxNormal", HideEditConditionToggle, InvalidEnumValues="None"))
	EPCGExMinimalAxis NormalAxis = EPCGExMinimalAxis::Z;

	/** Include the vertex position in the oriented bounding box calculation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_NotOverridable))
	bool bIncludeVtxInOOB = false;

	bool WantsOOB() const;

private:
	friend class FPCGExWriteVtxPropertiesElement;
};

struct FPCGExWriteVtxPropertiesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExWriteVtxPropertiesElement;

	TArray<TObjectPtr<const UPCGExVtxPropertyFactoryData>> ExtraFactories;

	PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExWriteVtxPropertiesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WriteVtxProperties)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWriteVtxProperties
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExWriteVtxPropertiesContext, UPCGExWriteVtxPropertiesSettings>
	{
		friend class FBatch;

		bool bWantsOOB = false;
		EAxis::Type NormalAxis = EAxis::Type::Z;
		TArray<TSharedPtr<FPCGExVtxPropertyOperation>> Operations;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
		virtual void Cleanup() override;

		PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_OUTPUT_DECL)
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_OUTPUT_DECL)

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);
		virtual ~FBatch() override;

		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
