// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExInfluenceDetails.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExRelaxClusters.generated.h"

#define PCGEX_FOREACH_FIELD_RELAX_CLUSTER(MACRO)\
MACRO(DirectionAndSize, FVector, FVector::ZeroVector)\
MACRO(Direction, FVector, FVector::ZeroVector)\
MACRO(Amplitude, double, 0)

class UPCGExRelaxClusterOperation;

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/relax-cluster"))
class UPCGExRelaxClustersSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(RelaxClusters, "Cluster : Relax", "Relax point positions using edges connecting them.", (Relaxing ? FName(Relaxing.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));
#endif

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Number of relaxation iterations to perform. More iterations = smoother result. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=1))
	int32 Iterations = 10;

	/** Influence Settings*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInfluenceDetails InfluenceDetails;

	/** Relaxing arithmetics */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExRelaxClusterOperation> Relaxing;


	/** Write the final direction and size of the relaxation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDirectionAndSize = false;

	/** Name of the 'FVector' attribute to write direction and size to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="DirectionAndSize", PCG_Overridable, EditCondition="bWriteDirectionAndSize"))
	FName DirectionAndSizeAttributeName = FName("DirectionAndSize");

	/** Write the final direction of the relaxation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDirection = false;

	/** Name of the 'FVector' attribute to write direction to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Direction", PCG_Overridable, EditCondition="bWriteDirection"))
	FName DirectionAttributeName = FName("Direction");

	/** Write the final amplitude of the relaxation. (that's the size of the DirectionAndSize vector) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAmplitude = false;

	/** Name of the 'double' attribute to write amplitude to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Amplitude", PCG_Overridable, EditCondition="bWriteAmplitude"))
	FName AmplitudeAttributeName = FName("Amplitude");

private:
	friend class FPCGExRelaxClustersElement;
};

struct FPCGExRelaxClustersContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExRelaxClustersElement;

	PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_DECL_TOGGLE)

	UPCGExRelaxClusterOperation* Relaxing = nullptr;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExRelaxClustersElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(RelaxClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExRelaxClusters
{
	const FName SourceOverridesRelaxing = TEXT("Overrides : Relaxing");

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExRelaxClustersContext, UPCGExRelaxClustersSettings>
	{
		int32 Iterations = 10;
		int32 Steps = 10;
		int32 CurrentStep = 0;
		EPCGExClusterElement StepSource = EPCGExClusterElement::Vtx;

		UPCGExRelaxClusterOperation* RelaxOperation = nullptr;

		TSharedPtr<TArray<FTransform>> PrimaryBuffer;
		TSharedPtr<TArray<FTransform>> SecondaryBuffer;

		FPCGExInfluenceDetails InfluenceDetails;

		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxDistanceValue;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_DECL)

		virtual ~FProcessor() override;

		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void StartNextStep();
		void RelaxScope(const PCGExMT::FScope& Scope) const;
		virtual void PrepareLoopScopesForNodes(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;
		virtual void OnNodesProcessingComplete() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_DECL)

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);
		virtual ~FBatch() override;

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
