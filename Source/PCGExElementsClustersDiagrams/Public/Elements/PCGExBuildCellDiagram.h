// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExBuildCellDiagram.generated.h"

namespace PCGExBlending
{
	class FUnionBlender;
}

namespace PCGExClusters
{
	class FProjectedPointSet;
	class FCellConstraints;
	class FCell;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExBuildCellDiagram
{
	class FProcessor;
}

UENUM()
enum class EPCGExCellSpokeMode : uint8
{
	None          = 0 UMETA(DisplayName = "None", ToolTip="Do not emit any centroid-to-corner spokes."),
	AllCorners    = 1 UMETA(DisplayName = "All Corners", ToolTip="Emit a spoke from each cell centroid to every one of its corners."),
	LongestSpoke  = 2 UMETA(DisplayName = "Longest Spoke", ToolTip="Emit only the single longest centroid-to-corner spoke per cell."),
	ShortestSpoke = 3 UMETA(DisplayName = "Shortest Spoke", ToolTip="Emit only the single shortest centroid-to-corner spoke per cell."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-cell-diagram"))
class UPCGExBuildCellDiagramSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildCellDiagram, "Cluster : Cell Diagram", "Creates a graph from cell adjacency relationships. Points are cell centroids, edges connect adjacent cells.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_BLEND(ClusterGenerator, Pathfinding);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings


	/** Cell constraints for filtering which cells become graph nodes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints = FPCGExCellConstraintsDetails(false);

	/** Projection settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Graph output settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Extract a topological "cell skeleton": a cell with exactly two neighbors drops its centroid and bridges its two shared-side midpoints with a single edge, while one-neighbor (leaf) and three-or-more-neighbor (junction) cells keep their centroid. Forces shared-midpoint routing internally and re-scopes Spoke Mode to leaf cells' unshared corners. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivisions", meta = (PCG_Overridable))
	bool bExtractSkeleton = false;

	/** How to emit additional edges connecting each cell centroid to its corner vertices. Corners are shared between adjacent cells. When Extract Skeleton is on, this is re-scoped to leaf (single-neighbor) cells and their unshared corners only: None = no spokes, All Corners = every unshared corner, Longest/Shortest = the single farthest/nearest unshared corner. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivisions", meta = (PCG_Overridable))
	EPCGExCellSpokeMode SpokeMode = EPCGExCellSpokeMode::None;

	/** In a single-spoke mode, also connect every other cell that shares an elected corner to that corner (in addition to its own spoke), turning the corner into a shared hub. A cell may then emit multiple spokes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivisions", meta = (PCG_Overridable, EditCondition = "(SpokeMode == EPCGExCellSpokeMode::LongestSpoke || SpokeMode == EPCGExCellSpokeMode::ShortestSpoke) && !bExtractSkeleton", EditConditionHides))
	bool bConnectSharedSelectedCorners = false;

	/** Replace each centroid-to-centroid adjacency edge with two edges routed through a new vertex at the midpoint of the shared cell segment (centroid -> midpoint -> centroid). Forced on internally when Extract Skeleton is enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivisions", meta = (PCG_Overridable, EditCondition = "!bExtractSkeleton", EditConditionHides))
	bool bSplitCellEdgesAtSharedMidpoint = false;

	/** Write cell area to centroid points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteArea = false;

	/** Attribute name for cell area */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteArea"))
	FName AreaAttributeName = FName("Area");

	/** Write cell compactness to centroid points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCompactness = false;

	/** Attribute name for cell compactness */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteCompactness"))
	FName CompactnessAttributeName = FName("Compactness");

	/** Write number of nodes in cell to centroid points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumNodes = false;

	/** Attribute name for node count */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteNumNodes"))
	FName NumNodesAttributeName = FName("NumNodes");

	/** Write a per-vertex type tag : 0 = centroid, 1 = corner, 2 = shared-segment midpoint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteVtxType = false;

	/** Attribute name for the vertex type tag */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteVtxType"))
	FName VtxTypeAttributeName = FName("VtxType");

	/** Write a per-edge type tag : 0 = cell adjacency, 1 = split-half (centroid<->midpoint), 2 = corner spoke, 3 = skeleton bridge (midpoint<->midpoint). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteEdgeType = false;

	/** Attribute name for the edge type tag */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteEdgeType"))
	FName EdgeTypeAttributeName = FName("EdgeType");

	/** Defines how cell vertex properties and attributes are blended to the centroid. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_Overridable))
	FPCGExBlendingDetails BlendingDetails = FPCGExBlendingDetails(EPCGExBlendingType::Average, EPCGExBlendingType::None);

	/** Meta filter settings for attribute carry-over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

private:
	friend class FPCGExBuildCellDiagramElement;
};

struct FPCGExBuildCellDiagramContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExBuildCellDiagramElement;

	TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
	TSharedPtr<PCGExData::FFacade> HolesFacade;
	FPCGExCarryOverDetails CarryOverDetails;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExBuildCellDiagramElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildCellDiagram)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildCellDiagram
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExBuildCellDiagramContext, UPCGExBuildCellDiagramSettings>
	{
	protected:
		TSharedPtr<PCGExData::FFacade> CentroidFacade;

		TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
		TArray<TSharedPtr<PCGExClusters::FCell>> ValidCells;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		// Cell adjacency
		TMap<int32, TSet<int32>> CellAdjacencyMap;
		TMap<int32, int32> FaceIndexToOutputIndex; // Maps face index to output point index

		TSharedPtr<PCGExBlending::FUnionBlender> UnionBlender;

		TSharedPtr<PCGExData::TBuffer<double>> AreaWriter = nullptr;
		TSharedPtr<PCGExData::TBuffer<double>> CompactnessWriter = nullptr;
		TSharedPtr<PCGExData::TBuffer<int32>> NumNodesWriter = nullptr;
		TSharedPtr<PCGExData::TBuffer<int32>> VtxTypeWriter = nullptr;

		// Output vertex layout as contiguous point-index blocks:
		// [0, NumCells) centroids | [CornerBlockStart, MidpointBlockStart) corners | [MidpointBlockStart, TotalVtxCount) midpoints
		int32 NumCells = 0;
		int32 CornerBlockStart = 0;
		int32 MidpointBlockStart = 0;
		int32 TotalVtxCount = 0;

		// Corner block (spokes). Corners are shared : one slot per unique activated cluster node.
		TArray<int32> CornerNodes;                    // corner slot -> cluster node index
		TMap<int32, int32> CornerNodeToSlot;          // cluster node index -> corner slot
		TMap<int32, TArray<int32>> CornerNodeToCells; // cluster node index -> ValidCells indices containing it
		TArray<int32> CellPickedCornerNode;           // ValidCells index -> elected corner node; populated only for the non-spread single-spoke pass

		// Midpoint block (split). One slot per shared cell segment between two valid cells.
		TArray<TPair<int32, int32>> MidpointNodePairs; // midpoint slot -> (origin node, target node) of shared segment
		TArray<TPair<int32, int32>> MidpointCentroids; // midpoint slot -> (centroid A, centroid B) output indices

		// Skeleton mode (bExtractSkeleton), centroid-indexed. NeighborCount = number of incident shared-side
		// midpoints : 2 -> collapse (drop centroid, bridge the two midpoints), 1 -> leaf, 3+ -> junction, 0 -> island.
		TArray<int32> CellNeighborCount;   // centroid index -> incident midpoint count
		TArray<int32> CellMidSlot0;        // centroid index -> first incident midpoint slot (INDEX_NONE if none)
		TArray<int32> CellMidSlot1;        // centroid index -> second incident midpoint slot (INDEX_NONE if < 2)
		TArray<int32> CornerSlotOwnerCell; // corner slot -> owning leaf centroid index (skeleton leaf-tip spokes)

		void SetupCornerBlock();
		void SetupSkeletonCornerBlock();
		void SetupMidpointBlock();
		void ClassifySkeletonCells();
		void SetupEdgeTypeTagging();

	public:
		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		virtual void Cleanup() override;
	};
}
