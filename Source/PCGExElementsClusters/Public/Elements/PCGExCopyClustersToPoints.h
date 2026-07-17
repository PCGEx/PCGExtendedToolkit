// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Fitting/PCGExFitting.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Utils/PCGExCompare.h"


#include "PCGExCopyClustersToPoints.generated.h"

namespace PCGExMatching
{
	class FDataMatcher;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/transform/cluster-copy-to-points"))
class UPCGExCopyClustersToPointsSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(CopyClustersToPoints, "Cluster : Copy to Points", "Create a copies of the input clusters onto the target points.  NOTE: Does not sanitize input.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterOp);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExClustersProcessorSettings interface
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExClustersProcessorSettings interface

	/** If enabled, allows you to pick which input gets copied to which target point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Cluster);

	/** Target inherit behavior */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTransformDetails TransformDetails;

	/** Copy target point attributes as tags on output clusters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding")
	FPCGExAttributeToTagDetails TargetsAttributesToClusterTags;

	/** Which target attributes to forward on clusters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding")
	FPCGExForwardDetails TargetsForwarding;
};

struct FPCGExCopyClustersToPointsContext final : FPCGExClustersProcessorContext
{
	friend class UPCGExCopyClustersToPointsSettings;
	friend class FPCGExCopyClustersToPointsElement;

	FPCGExTransformDetails TransformDetails;

	TSharedPtr<PCGExData::FFacade> TargetsDataFacade;
	TSharedPtr<PCGExMatching::FDataMatcher> MainDataMatcher;
	TSharedPtr<PCGExMatching::FDataMatcher> EdgeDataMatcher;

	FPCGExAttributeToTagDetails TargetsAttributesToClusterTags;
	TSharedPtr<PCGExData::FDataForwardHandler> TargetsForwardHandler;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExCopyClustersToPointsElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CopyClustersToPoints)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExCopyClustersToPoints
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExCopyClustersToPointsContext, UPCGExCopyClustersToPointsSettings>
	{
		friend class FBatch;

	protected:
		int32 NumCopies = 0;
		PCGExMatching::FScope MatchScope;
		PCGExMatching::FScope InfiniteScope;

	public:
		TArray<TSharedPtr<PCGExData::FPointIO>>* VtxDupes = nullptr;
		TArray<PCGExDataId>* VtxTag = nullptr;

		TArray<TSharedPtr<PCGExData::FPointIO>> EdgesDupes;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			bBuildCluster = false;
		}

		virtual ~FProcessor() override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
		virtual void CompleteWork() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	protected:
		int32 NumCopies = 0;

	public:
		TArray<TSharedPtr<PCGExData::FPointIO>> VtxDupes;
		TArray<PCGExDataId> VtxTag;

		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
		}

		virtual ~FBatch() override;
		virtual void Process() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void CompleteWork() override;
	};
}
