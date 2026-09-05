// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildCellDiagram.h"

#include "Blenders/PCGExUnionBlender.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExSubGraph.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Math/PCGExMathDistances.h"
#include "Sampling/PCGExSamplingUnionData.h"

#define LOCTEXT_NAMESPACE "PCGExBuildCellDiagram"
#define PCGEX_NAMESPACE BuildCellDiagram

TArray<FPCGPinProperties> UPCGExBuildCellDiagramSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExClusters::Labels::SourceHolesLabel, "Omit cells that contain any points from this dataset", Normal)
	return PinProperties;
}

PCGExData::EIOInit UPCGExBuildCellDiagramSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExBuildCellDiagramSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(BuildCellDiagram)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(BuildCellDiagram)

bool FPCGExBuildCellDiagramElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildCellDiagram)

	// Validate attribute names
	if (Settings->bWriteArea)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->AreaAttributeName);
	}
	if (Settings->bWriteCompactness)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->CompactnessAttributeName);
	}
	if (Settings->bWriteNumNodes)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->NumNodesAttributeName);
	}
	if (Settings->bWriteVtxType)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->VtxTypeAttributeName);
	}
	if (Settings->bWriteEdgeType)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->EdgeTypeAttributeName);
	}

	Context->HolesFacade = PCGExData::TryGetSingleFacade(Context, PCGExClusters::Labels::SourceHolesLabel, false, false);
	if (Context->HolesFacade && Settings->ProjectionDetails.Method == EPCGExProjectionMethod::Normal)
	{
		Context->Holes = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), Settings->ProjectionDetails);
		Context->Holes->EnsureProjected();
	}

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	return true;
}

bool FPCGExBuildCellDiagramElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildCellDiagramElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildCellDiagram)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
				NewBatch->SetProjectionDetails(Settings->ProjectionDetails);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}


namespace PCGExBuildCellDiagram
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildCellDiagram::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Context->HolesFacade)
		{
			Holes = Context->Holes ? Context->Holes : MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), ProjectionDetails);
			if (Holes)
			{
				Holes->EnsureProjected();
			}
		}

		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->Reserve(Cluster->Edges->Num());
		CellsConstraints->Holes = Holes;

		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		// Enumerate all cells (wrapper is omitted by default for graph)
		Enumerator->EnumerateAllFaces(ValidCells, CellsConstraints.ToSharedRef(), nullptr, true);

		NumCells = ValidCells.Num();
		if (NumCells < 2)
		{
			// Need at least 2 cells to form a graph
			bIsProcessorValid = false;
			return true;
		}

		// Get adjacency map (cached in enumerator)
		int32 WrapperFaceIndex = Enumerator->GetWrapperFaceIndex();
		CellAdjacencyMap = Enumerator->GetOrBuildAdjacencyMap(WrapperFaceIndex);

		// Build FaceIndex -> OutputIndex mapping
		for (int32 i = 0; i < NumCells; ++i)
		{
			if (ValidCells[i] && ValidCells[i]->FaceIndex >= 0)
			{
				FaceIndexToOutputIndex.Add(ValidCells[i]->FaceIndex, i);
			}
		}

		// Resolve the additional vertex blocks appended after the centroids. Skeleton mode forces shared-midpoint
		// routing and re-scopes spokes to leaf cells' unshared corners, so it needs the midpoint block built and
		// the cells classified before the corner block is resolved.
		const bool bSkeleton = Settings->bExtractSkeleton;
		const bool bUseMidpoints = bSkeleton || Settings->bSplitCellEdgesAtSharedMidpoint;

		if (bUseMidpoints)
		{
			SetupMidpointBlock();
		}

		if (bSkeleton)
		{
			ClassifySkeletonCells();
			if (Settings->SpokeMode != EPCGExCellSpokeMode::None)
			{
				SetupSkeletonCornerBlock();
			}
		}
		else if (Settings->SpokeMode != EPCGExCellSpokeMode::None)
		{
			SetupCornerBlock();
		}

		CornerBlockStart = NumCells;
		MidpointBlockStart = NumCells + CornerNodes.Num();
		TotalVtxCount = MidpointBlockStart + MidpointNodePairs.Num();

		// Create output vertex data (cell centroids + optional corners/midpoints)
		TSharedPtr<PCGExData::FPointIO> CentroidIO = Context->MainPoints->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
		Context->CarryOverDetails.Prune(CentroidIO->Tags.Get());
		CentroidIO->IOIndex = BatchIndex;
		PCGExClusters::Helpers::CleanupClusterData(CentroidIO);

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(CentroidIO->GetOut(), TotalVtxCount);

		CentroidFacade = MakeShared<PCGExData::FFacade>(CentroidIO.ToSharedRef());

		// Create and initialize union blender
		UnionBlender = MakeShared<PCGExBlending::FUnionBlender>(
			const_cast<FPCGExBlendingDetails*>(&Settings->BlendingDetails),
			&Context->CarryOverDetails,
			PCGExMath::GetNoneDistances());

		TArray<TSharedRef<PCGExData::FFacade>> BlendSources;
		BlendSources.Add(VtxDataFacade);
		UnionBlender->AddSources(BlendSources, &PCGExClusters::Labels::ProtectedClusterAttributes);

		if (!UnionBlender->Init(Context, CentroidFacade))
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Failed to initialize blender for cell diagram."));
		}

		// Create attribute writers (after blender init so they aren't captured)
		if (Settings->bWriteArea)
		{
			AreaWriter = CentroidFacade->GetWritable<double>(Settings->AreaAttributeName, 0.0, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bWriteCompactness)
		{
			CompactnessWriter = CentroidFacade->GetWritable<double>(Settings->CompactnessAttributeName, 0.0, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bWriteNumNodes)
		{
			NumNodesWriter = CentroidFacade->GetWritable<int32>(Settings->NumNodesAttributeName, 0, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bWriteVtxType)
		{
			VtxTypeWriter = CentroidFacade->GetWritable<int32>(Settings->VtxTypeAttributeName, 0, true, PCGExData::EBufferInit::New);
		}

		StartParallelLoopForRange(TotalVtxCount);

		return true;
	}

	void FProcessor::SetupCornerBlock()
	{
		const EPCGExCellSpokeMode Mode = Settings->SpokeMode;
		const bool bAllCorners = Mode == EPCGExCellSpokeMode::AllCorners;
		const bool bSingleMode = Mode == EPCGExCellSpokeMode::LongestSpoke || Mode == EPCGExCellSpokeMode::ShortestSpoke;
		const bool bLongest = Mode == EPCGExCellSpokeMode::LongestSpoke;

		// Per-cell elected corners are only consumed by the non-spread single-spoke edge pass; in All/spread
		// modes spokes come from CornerNodeToCells, so we don't allocate or fill them there.
		const bool bNeedPicks = bSingleMode && !Settings->bConnectSharedSelectedCorners;
		if (bNeedPicks)
		{
			CellPickedCornerNode.Init(INDEX_NONE, NumCells);
		}

		// Materialize an activated corner as a shared vertex slot : one slot per unique cluster node.
		auto ActivateCorner = [&](const int32 NodeIdx)
		{
			if (!CornerNodeToSlot.Contains(NodeIdx))
			{
				CornerNodeToSlot.Add(NodeIdx, CornerNodes.Num());
				CornerNodes.Add(NodeIdx);
			}
		};

		// Single pass : map each corner node to the cells that use it (drives the shared-corner spread),
		// activate corners (every corner in All mode, the elected one in a single mode), and in a single
		// mode elect this cell's longest/shortest corner by centroid distance. Cells excluded from output
		// connectivity (FaceIndex < 0, i.e. absent from FaceIndexToOutputIndex) are skipped so spokes stay
		// consistent with the cell-to-cell edges.
		for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
		{
			const TSharedPtr<PCGExClusters::FCell>& Cell = ValidCells[CellIdx];
			if (!Cell || Cell->Nodes.IsEmpty() || Cell->FaceIndex < 0)
			{
				continue;
			}

			for (const int32 NodeIdx : Cell->Nodes)
			{
				CornerNodeToCells.FindOrAdd(NodeIdx).AddUnique(CellIdx);
				if (bAllCorners)
				{
					ActivateCorner(NodeIdx);
				}
			}

			if (bSingleMode)
			{
				const FVector Centroid = Cell->Data.Centroid;
				int32 BestNode = INDEX_NONE;
				double BestDistSq = bLongest ? -1.0 : TNumericLimits<double>::Max();
				for (const int32 NodeIdx : Cell->Nodes)
				{
					const double DistSq = FVector::DistSquared(Centroid, Cluster->GetPos(NodeIdx));
					if (bLongest ? DistSq > BestDistSq : DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestNode = NodeIdx;
					}
				}
				if (bNeedPicks)
				{
					CellPickedCornerNode[CellIdx] = BestNode;
				}
				if (BestNode != INDEX_NONE)
				{
					ActivateCorner(BestNode);
				}
			}
		}
	}

	void FProcessor::SetupMidpointBlock()
	{
		const TSharedPtr<PCGExClusters::FPlanarFaceEnumerator>& Enumerator = CellsConstraints->Enumerator;
		if (!Enumerator)
		{
			return;
		}

		TArray<PCGExClusters::FSharedSegment> Segments;
		Enumerator->GetSharedSegments(Segments);

		// One midpoint per shared segment between two valid output cells. Segments whose faces were
		// constraint-filtered (or the wrapper) are absent from FaceIndexToOutputIndex and produce no midpoint.
		for (const PCGExClusters::FSharedSegment& Seg : Segments)
		{
			const int32* OutAPtr = FaceIndexToOutputIndex.Find(Seg.FaceA);
			const int32* OutBPtr = FaceIndexToOutputIndex.Find(Seg.FaceB);
			if (!OutAPtr || !OutBPtr)
			{
				continue;
			}

			MidpointNodePairs.Emplace(Seg.OriginNode, Seg.TargetNode);
			MidpointCentroids.Emplace(*OutAPtr, *OutBPtr);
		}
	}

	void FProcessor::ClassifySkeletonCells()
	{
		// Count incident shared-side midpoints per centroid and remember up to the first two slots. A centroid
		// with exactly two incident midpoints collapses (its two side-midpoints are bridged directly and the
		// centroid is dropped); one-neighbor (leaf) and 3+-neighbor (junction) centroids are kept. Centroids
		// with no incident midpoint contribute nothing and are pruned as isolated points at compile time.
		CellNeighborCount.Init(0, NumCells);
		CellMidSlot0.Init(INDEX_NONE, NumCells);
		CellMidSlot1.Init(INDEX_NONE, NumCells);

		const int32 NumMidpoints = MidpointCentroids.Num();
		for (int32 Slot = 0; Slot < NumMidpoints; ++Slot)
		{
			const TPair<int32, int32>& Centroids = MidpointCentroids[Slot];
			const int32 Cells[2] = {Centroids.Key, Centroids.Value};
			for (const int32 CellIdx : Cells)
			{
				const int32 Count = CellNeighborCount[CellIdx]++;
				if (Count == 0) { CellMidSlot0[CellIdx] = Slot; }
				else if (Count == 1) { CellMidSlot1[CellIdx] = Slot; }
			}
		}
	}

	void FProcessor::SetupSkeletonCornerBlock()
	{
		// Leaf-tip spokes : only leaf cells (exactly one neighbor, which keep their centroid) spoke out, and only
		// to their unshared corners -- cluster nodes that belong to no other valid cell. Spoke Mode selects how
		// many : All Corners -> every unshared corner, Longest/Shortest -> the single farthest/nearest one.
		const EPCGExCellSpokeMode Mode = Settings->SpokeMode;
		const bool bAll = Mode == EPCGExCellSpokeMode::AllCorners;
		const bool bLongest = Mode == EPCGExCellSpokeMode::LongestSpoke;

		// Count how many valid cells each cluster node belongs to; a node used by exactly one cell is "unshared".
		TMap<int32, int32> NodeCellCount;
		for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
		{
			const TSharedPtr<PCGExClusters::FCell>& Cell = ValidCells[CellIdx];
			if (!Cell || Cell->Nodes.IsEmpty() || Cell->FaceIndex < 0)
			{
				continue;
			}
			for (const int32 NodeIdx : Cell->Nodes)
			{
				NodeCellCount.FindOrAdd(NodeIdx)++;
			}
		}

		// One corner slot per activated unshared corner. Each unshared corner belongs to exactly one cell, so
		// there is no sharing : record its single owning leaf centroid alongside the slot for edge emission.
		auto AddSpokeCorner = [&](const int32 CellIdx, const int32 NodeIdx)
		{
			CornerNodeToSlot.Add(NodeIdx, CornerNodes.Num());
			CornerNodes.Add(NodeIdx);
			CornerSlotOwnerCell.Add(CellIdx);
		};

		for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
		{
			if (CellNeighborCount[CellIdx] != 1) // leaf cells only
			{
				continue;
			}

			const TSharedPtr<PCGExClusters::FCell>& Cell = ValidCells[CellIdx];
			if (!Cell || Cell->Nodes.IsEmpty() || Cell->FaceIndex < 0)
			{
				continue;
			}

			if (bAll)
			{
				for (const int32 NodeIdx : Cell->Nodes)
				{
					const int32* CountPtr = NodeCellCount.Find(NodeIdx);
					if (CountPtr && *CountPtr == 1)
					{
						AddSpokeCorner(CellIdx, NodeIdx);
					}
				}
			}
			else // single longest/shortest among the unshared corners
			{
				const FVector Centroid = Cell->Data.Centroid;
				int32 BestNode = INDEX_NONE;
				double BestDistSq = bLongest ? -1.0 : TNumericLimits<double>::Max();
				for (const int32 NodeIdx : Cell->Nodes)
				{
					const int32* CountPtr = NodeCellCount.Find(NodeIdx);
					if (!CountPtr || *CountPtr != 1)
					{
						continue;
					}
					const double DistSq = FVector::DistSquared(Centroid, Cluster->GetPos(NodeIdx));
					if (bLongest ? DistSq > BestDistSq : DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestNode = NodeIdx;
					}
				}
				if (BestNode != INDEX_NONE)
				{
					AddSpokeCorner(CellIdx, BestNode);
				}
			}
		}
	}

	void FProcessor::SetupEdgeTypeTagging()
	{
		// Classify each edge by the vertex blocks of its endpoints. This runs on the graph's edges -- whose
		// Start/End are still our original [centroids | corners | midpoints] point indices -- BEFORE compilation
		// Morton-sorts and renumbers the points. The result is keyed by the stable parent edge index so
		// OnPreCompile can recover it per output edge via EdgeKeys. (FlattenedEdges endpoints are post-sort
		// point indices, so range-classifying them directly would misclassify every edge.)
		const int32 CornerStart = CornerBlockStart;
		const int32 MidStart = MidpointBlockStart;

		const TArray<PCGExGraphs::FEdge>& Edges = GraphBuilder->Graph->Edges;
		const TSharedPtr<TArray<int8>> EdgeTypeByIndex = MakeShared<TArray<int8>>();
		EdgeTypeByIndex->SetNumZeroed(Edges.Num());
		for (const PCGExGraphs::FEdge& E : Edges)
		{
			const int32 A = static_cast<int32>(E.Start);
			const int32 B = static_cast<int32>(E.End);
			const bool bAMid = A >= MidStart;
			const bool bBMid = B >= MidStart;

			int8 Type = 0;                                               // cell adjacency (centroid <-> centroid)
			if (bAMid && bBMid) { Type = 3; }                            // skeleton bridge (midpoint <-> midpoint)
			else if (bAMid || bBMid) { Type = 1; }                       // split-half (centroid <-> midpoint)
			else if (A >= CornerStart || B >= CornerStart) { Type = 2; } // corner spoke (touches a corner)
			(*EdgeTypeByIndex)[E.Index] = Type;
		}

		const FName TypeName = Settings->EdgeTypeAttributeName;

		// A non-null user context is required for OnPreCompile to fire.
		GraphBuilder->OnCreateContext = []() -> TSharedPtr<PCGExGraphs::FSubGraphUserContext>
		{
			return MakeShared<PCGExGraphs::FSubGraphUserContext>();
		};

		GraphBuilder->OnPreCompile = [TypeName, EdgeTypeByIndex](PCGExGraphs::FSubGraphUserContext&, const PCGExGraphs::FSubGraphPreCompileData& Data)
		{
			const TSharedPtr<PCGExData::TBuffer<int32>> TypeBuffer = Data.EdgesDataFacade->GetWritable<int32>(TypeName, 0, true, PCGExData::EBufferInit::New);
			if (!TypeBuffer)
			{
				return;
			}

			const int32 NumEdges = Data.FlattenedEdges.Num();
			for (int32 i = 0; i < NumEdges; ++i)
			{
				// EdgeKeys[i].Index is the edge's index in the parent graph -- stable across point/edge renumbering.
				const int32 ParentEdgeIndex = Data.EdgeKeys[i].Index;
				TypeBuffer->SetValue(i, EdgeTypeByIndex->IsValidIndex(ParentEdgeIndex) ? (*EdgeTypeByIndex)[ParentEdgeIndex] : 0);
			}
		};
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		UPCGBasePointData* CentroidIO = CentroidFacade->GetOut();

		TPCGValueRange<FTransform> OutTransforms = CentroidIO->GetTransformValueRange();
		TPCGValueRange<FVector> OutBoundsMin = CentroidIO->GetBoundsMinValueRange();
		TPCGValueRange<FVector> OutBoundsMax = CentroidIO->GetBoundsMaxValueRange();

		// Per-task (per-scope) blend scratch -- never shared across scopes.
		TArray<PCGExData::FWeightedPoint> WeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		UnionBlender->InitTrackers(Trackers);

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();
		const int32 SourceIOIndex = VtxDataFacade->Source->IOIndex;

		// Place one output vertex : write its transform/bounds, blend the source cluster nodes (equal weight)
		// into its row, and tag its type. Each Index owns a disjoint row, so writes are race-free across scopes.
		auto WriteBlendedVertex = [&](const int32 Index, const FVector& Position, const FVector& HalfExtent, const TArrayView<const int32>& SourceNodes, const int32 VtxType)
		{
			FTransform Transform = FTransform::Identity;
			Transform.SetLocation(Position);
			OutTransforms[Index] = Transform;
			OutBoundsMin[Index] = -HalfExtent;
			OutBoundsMax[Index] = HalfExtent;

			Union->Reset();
			Union->Reserve(1, SourceNodes.Num());
			for (const int32 NodeIdx : SourceNodes)
			{
				Union->AddWeighted_Unsafe(PCGExData::FElement(Cluster->GetNodePointIndex(NodeIdx), SourceIOIndex), 1.0);
			}

			UnionBlender->ComputeWeights(Index, Union, WeightedPoints);
			UnionBlender->Blend(Index, WeightedPoints, Trackers);

			if (VtxTypeWriter) { VtxTypeWriter->SetValue(Index, VtxType); }
		};

		PCGEX_SCOPE_LOOP(Index)
		{
			if (Index < CornerBlockStart)
			{
				// ===== Centroid ===== (blend all cell vertices, keep the cell bounds and cell metrics)
				const TSharedPtr<PCGExClusters::FCell>& Cell = ValidCells[Index];
				if (!Cell)
				{
					continue;
				}

				WriteBlendedVertex(Index, Cell->Data.Centroid, Cell->Data.Bounds.GetExtent(), Cell->Nodes, 0);

				if (AreaWriter) { AreaWriter->SetValue(Index, Cell->Data.Area); }
				if (CompactnessWriter) { CompactnessWriter->SetValue(Index, Cell->Data.Compactness); }
				if (NumNodesWriter) { NumNodesWriter->SetValue(Index, Cell->Nodes.Num()); }
			}
			else if (Index < MidpointBlockStart)
			{
				// ===== Corner ===== (a corner IS a cluster node : copy it, single source)
				const int32 NodeIdx = CornerNodes[Index - CornerBlockStart];
				WriteBlendedVertex(Index, Cluster->GetPos(NodeIdx), FVector::ZeroVector, MakeArrayView(&NodeIdx, 1), 1);
			}
			else
			{
				// ===== Shared-segment midpoint ===== (blend the two segment endpoints, equal weight)
				const TPair<int32, int32>& Pair = MidpointNodePairs[Index - MidpointBlockStart];
				const int32 SourceNodes[2] = {Pair.Key, Pair.Value};
				WriteBlendedVertex(Index, (Cluster->GetPos(Pair.Key) + Cluster->GetPos(Pair.Value)) * 0.5, FVector::ZeroVector, MakeArrayView(SourceNodes, 2), 2);
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		TSet<uint64> UniqueEdges;

		// ---- Cell-to-cell connectivity ----
		const bool bSkeleton = Settings->bExtractSkeleton;
		const bool bUseMidpoints = bSkeleton || Settings->bSplitCellEdgesAtSharedMidpoint;

		if (bUseMidpoints)
		{
			if (bSkeleton)
			{
				// Skeleton routing : a centroid with exactly two incident midpoints is collapsed -- its two
				// side-midpoints are bridged directly and the centroid is dropped (left edgeless, so it gets
				// pruned as an isolated point at compile time). Leaf (one) and junction (3+) centroids keep
				// their centroid <-> midpoint edges.
				for (int32 Slot = 0; Slot < MidpointNodePairs.Num(); ++Slot)
				{
					const int32 MidOut = MidpointBlockStart + Slot;
					const TPair<int32, int32>& Centroids = MidpointCentroids[Slot];
					if (CellNeighborCount[Centroids.Key] != 2) { UniqueEdges.Add(PCGEx::H64U(Centroids.Key, MidOut)); }
					if (CellNeighborCount[Centroids.Value] != 2) { UniqueEdges.Add(PCGEx::H64U(MidOut, Centroids.Value)); }
				}

				// Bridge the two side-midpoints of each collapsed (two-neighbor) cell.
				for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
				{
					if (CellNeighborCount[CellIdx] != 2) { continue; }
					UniqueEdges.Add(PCGEx::H64U(MidpointBlockStart + CellMidSlot0[CellIdx], MidpointBlockStart + CellMidSlot1[CellIdx]));
				}
			}
			else
			{
				// Route each adjacency through its shared-segment midpoint vertex : centroid -> midpoint -> centroid
				for (int32 Slot = 0; Slot < MidpointNodePairs.Num(); ++Slot)
				{
					const int32 MidOut = MidpointBlockStart + Slot;
					const TPair<int32, int32>& Centroids = MidpointCentroids[Slot];
					UniqueEdges.Add(PCGEx::H64U(Centroids.Key, MidOut));
					UniqueEdges.Add(PCGEx::H64U(MidOut, Centroids.Value));
				}
			}
		}
		else
		{
			// Direct centroid-to-centroid adjacency
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (!Cell || Cell->FaceIndex < 0)
				{
					continue;
				}

				const int32* PointAPtr = FaceIndexToOutputIndex.Find(Cell->FaceIndex);
				if (!PointAPtr)
				{
					continue;
				}
				const int32 PointA = *PointAPtr;

				if (const TSet<int32>* Adjacent = CellAdjacencyMap.Find(Cell->FaceIndex))
				{
					for (int32 AdjFace : *Adjacent)
					{
						const int32* PointBPtr = FaceIndexToOutputIndex.Find(AdjFace);
						if (!PointBPtr)
						{
							continue;
						}
						// Use H64U to ensure unique edges (A,B) == (B,A)
						UniqueEdges.Add(PCGEx::H64U(PointA, *PointBPtr));
					}
				}
			}
		}

		// ---- Corner spokes ----
		if (bSkeleton)
		{
			// Leaf-tip spokes : each activated unshared corner connects to its single owning leaf centroid.
			for (int32 Slot = 0; Slot < CornerNodes.Num(); ++Slot)
			{
				UniqueEdges.Add(PCGEx::H64U(CornerSlotOwnerCell[Slot], CornerBlockStart + Slot));
			}
		}
		else if (Settings->SpokeMode != EPCGExCellSpokeMode::None)
		{
			// In All mode (or when the spread toggle is on) every cell touching an activated corner spokes to it.
			// In a single-spoke mode without spread, each cell connects only to its own elected corner.
			const bool bSpread = Settings->SpokeMode == EPCGExCellSpokeMode::AllCorners || Settings->bConnectSharedSelectedCorners;

			if (bSpread)
			{
				for (int32 Slot = 0; Slot < CornerNodes.Num(); ++Slot)
				{
					const int32 CornerOut = CornerBlockStart + Slot;
					if (const TArray<int32>* Cells = CornerNodeToCells.Find(CornerNodes[Slot]))
					{
						for (const int32 CellIdx : *Cells)
						{
							UniqueEdges.Add(PCGEx::H64U(CellIdx, CornerOut));
						}
					}
				}
			}
			else
			{
				for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
				{
					const int32 NodeIdx = CellPickedCornerNode[CellIdx];
					if (NodeIdx == INDEX_NONE)
					{
						continue;
					}
					if (const int32* SlotPtr = CornerNodeToSlot.Find(NodeIdx))
					{
						UniqueEdges.Add(PCGEx::H64U(CellIdx, CornerBlockStart + *SlotPtr));
					}
				}
			}
		}

		if (UniqueEdges.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		// Create graph and insert edges
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(CentroidFacade.ToSharedRef(), &Settings->GraphBuilderDetails);
		GraphBuilder->bInheritNodeData = false; // We created new points from scratch, don't inherit from input

		GraphBuilder->Graph = MakeShared<PCGExGraphs::FGraph>(CentroidFacade->GetNum(PCGExData::EIOSide::Out));
		GraphBuilder->Graph->InsertEdges(UniqueEdges, BatchIndex);

		// Set up edge output
		GraphBuilder->EdgesIO = Context->MainEdges;
		GraphBuilder->NodePointsTransforms = CentroidFacade->GetOut()->GetConstTransformValueRange();

		if (Settings->bWriteEdgeType)
		{
			SetupEdgeTypeTagging();
		}

		GraphBuilder->CompileAsync(TaskManager, true, nullptr);
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExBuildCellDiagramContext, UPCGExBuildCellDiagramSettings>::Cleanup();
		if (CellsConstraints)
		{
			CellsConstraints->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
