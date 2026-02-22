// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"

#include "Core/PCGExClustersProcessor.h"
#include "PCGExPathfindingFindClusterHull.generated.h"

namespace PCGExClusters
{
	class FCellConstraints;
}

namespace PCGExFindClusterHull
{
	class FProcessor;
}


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cells/find-cluster-hull"))
class UPCGExFindClusterHullSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FindClusterHull, "Pathfinding : Find Cluster Hull", "Output a single hull per cluster, as a path.");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Pathfinding); }
#endif

protected:
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints = FPCGExCellConstraintsDetails(true);

	/** Cell artifacts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellArtifactsDetails Artifacts;

	/** Output a filtered set of points containing only seeds that generated a valid path */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	//bool bOutputSeeds = false;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails(true);

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

	/** Suppress warning when a hull could not be found for a cluster. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bQuietFailedToFindHullWarning = false;

private:
	friend class FPCGExFindClusterHullElement;
};

struct FPCGExFindClusterHullContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFindClusterHullElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExCellArtifactsDetails Artifacts;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;
	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBounds;

	mutable FRWLock SeedOutputLock;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFindClusterHullElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindClusterHull)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindClusterHull
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindClusterHullContext, UPCGExFindClusterHullSettings>
	{
	public:
		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void ProcessCell(const TSharedPtr<PCGExClusters::FCell>& InCell);
	};
}
