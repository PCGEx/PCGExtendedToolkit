// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Math/PCGExProjectionDetails.h"
#include "PCGExConnectPoints.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExProbing
{
	class FProbingEngine;
}

class UPCGExProbeFactoryData;

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points"))
class UPCGExConnectPointsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ConnectPoints, "Cluster : Connect Points", "Connect points according to a set of probes");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	//~End UPCGExPointsProcessorSettings

	/** Prevent connections between coincident points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bPreventCoincidence = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bPreventCoincidence", ClampMin=0.00001, ClampMax=1))
	double CoincidenceTolerance = 0.001;

	/** Project points to 2D for connection testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bProjectPoints = false;

	/** Projection settings for 2D testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bProjectPoints", DisplayName="Project Points"))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);
};

struct FPCGExConnectPointsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExConnectPointsElement;

	TArray<TObjectPtr<const UPCGExProbeFactoryData>> ProbeFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> GeneratorsFiltersFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> ConnectablesFiltersFactories;

	FVector CWCoincidenceTolerance = FVector::OneVector;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExConnectPointsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ConnectPoints)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExConnectPoints
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExConnectPointsContext, UPCGExConnectPointsSettings>
	{
		TSharedPtr<PCGExPointFilter::FManager> GeneratorsFilter;
		TSharedPtr<PCGExPointFilter::FManager> ConnectableFilter;

		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		TSharedPtr<PCGExProbing::FProbingEngine> Engine;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void OnPreparationComplete();

		virtual void CompleteWork() override;
		virtual void Output() override;

		virtual void Cleanup() override;
	};
}
