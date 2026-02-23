// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Core/PCGExClustersProcessor.h"

#include "PCGExPathfindingFindAllCells.generated.h"

namespace PCGExClusters
{
	class FProjectedPointSet;
	class FCellConstraints;
	class FCellPathBuilder;
}

namespace PCGExFindAllCells
{
	class FProcessor;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cells/find-all-cells"))
class UPCGExFindAllCellsSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FindAllCells, "Pathfinding : Find All Cells", "Attempts to find the contours of all cluster cells.");
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

	/** Cell output settings (output mode, attributes, OBB settings) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellArtifactsDetails Artifacts;

	/** Hole growth settings. Expands hole exclusion to adjacent cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellGrowthDetails HoleGrowth;

	/** Output a filtered set of points containing only seeds that generated a valid path */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	//bool bOutputSeeds = false;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails(true);

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExFindAllCellsElement;
};

struct FPCGExFindAllCellsContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFindAllCellsElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExCellArtifactsDetails Artifacts;
	FPCGExCellGrowthDetails HoleGrowth;

	TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
	TSharedPtr<PCGExData::FFacade> HolesFacade;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;
	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBounds;
	TSharedPtr<PCGExData::FPointIO> Seeds;

	mutable FRWLock SeedOutputLock;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFindAllCellsElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindAllCells)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindAllCells
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindAllCellsContext, UPCGExFindAllCellsSettings>
	{
	protected:
		TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
		TSharedPtr<PCGExClusters::FCellPathBuilder> CellProcessor;
		TArray<TSharedPtr<PCGExClusters::FCell>> ValidCells;
		TArray<TSharedPtr<PCGExData::FPointIO>> CellsIO;

		// Hole expansion tracking
		TMap<int32, TSet<int32>> CellAdjacencyMap;
		TSet<int32> ExcludedFaceIndices;  // Face indices to exclude due to holes or growth

	public:
		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;

		/** Expand hole exclusion from initial cell to adjacent cells up to growth depth */
		void ExpandHoleExclusion(int32 HoleIndex, int32 InitialFaceIndex, int32 MaxGrowth);

		virtual void Cleanup() override;
	};
}
