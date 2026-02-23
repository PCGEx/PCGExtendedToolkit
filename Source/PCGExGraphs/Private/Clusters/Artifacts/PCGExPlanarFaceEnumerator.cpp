// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"

#include "Async/ParallelFor.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExMath.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/Geo/PCGExGeo.h"

namespace PCGExClusters
{
	void FPlanarFaceEnumerator::Build(const TSharedRef<FCluster>& InCluster, const FPCGExGeo2DProjectionDetails& InProjection)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::Build);

		Cluster = &InCluster.Get();

		const TArray<FNode>& Nodes = *Cluster->Nodes;
		const int32 NumNodes = Nodes.Num();

		// Build node-indexed projected positions
		ProjectedPositions = MakeShared<TArray<FVector2D>>();
		ProjectedPositions->SetNumUninitialized(NumNodes);

		TConstPCGValueRange<FTransform> VtxTransforms = Cluster->VtxTransforms;
		TArray<FVector2D>& Positions = *ProjectedPositions;

		for (int32 NodeIdx = 0; NodeIdx < NumNodes; ++NodeIdx)
		{
			const FVector Location = VtxTransforms[Nodes[NodeIdx].PointIndex].GetLocation();
			const FVector Projected = InProjection.Project(Location);
			Positions[NodeIdx] = FVector2D(Projected.X, Projected.Y);
		}

		// Delegate to the shared implementation
		Build(InCluster, ProjectedPositions);
	}

	void FPlanarFaceEnumerator::Build(const TSharedRef<FCluster>& InCluster, const TSharedPtr<TArray<FVector2D>>& InNodeIndexedPositions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::Build);

		Cluster = &InCluster.Get();
		ProjectedPositions = InNodeIndexedPositions;
		bIsLocalTangent = false;

		const TArray<FNode>& Nodes = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;
		PCGEx::FIndexLookup* NodeLookup = Cluster->NodeIndexLookup.Get();
		const int32 NumEdges = Edges.Num();
		const int32 NumNodes = Nodes.Num();
		const TArray<FVector2D>& Positions = *ProjectedPositions;

		// Step 1: Create all half-edges (2 per edge)
		HalfEdges.Reset();
		HalfEdges.Reserve(NumEdges * 2);
		HalfEdgeMap.Reset();
		HalfEdgeMap.Reserve(NumEdges * 2);

		for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; ++EdgeIdx)
		{
			const PCGExGraphs::FEdge& Edge = Edges[EdgeIdx];
			// Edge.Start and Edge.End are POINT indices, convert to node indices
			const int32 NodeA = NodeLookup->Get(Edge.Start);
			const int32 NodeB = NodeLookup->Get(Edge.End);

			// Get 2D positions using NODE indices (not point indices)
			const FVector2D& PosA = Positions[NodeA];
			const FVector2D& PosB = Positions[NodeB];

			// Half-edge A → B
			const FVector2D DirAB = (PosB - PosA).GetSafeNormal();
			const double AngleAB = FMath::Atan2(DirAB.Y, DirAB.X);
			const int32 IndexAB = HalfEdges.Num();
			HalfEdges.Emplace(NodeA, NodeB, AngleAB);
			HalfEdgeMap.Add(PCGEx::H64(NodeA, NodeB), IndexAB);

			// Half-edge B → A
			const FVector2D DirBA = (PosA - PosB).GetSafeNormal();
			const double AngleBA = FMath::Atan2(DirBA.Y, DirBA.X);
			const int32 IndexBA = HalfEdges.Num();
			HalfEdges.Emplace(NodeB, NodeA, AngleBA);
			HalfEdgeMap.Add(PCGEx::H64(NodeB, NodeA), IndexBA);

			// Link twins
			HalfEdges[IndexAB].TwinIndex = IndexBA;
			HalfEdges[IndexBA].TwinIndex = IndexAB;
		}

		// Step 2: For each vertex, collect and sort outgoing half-edges by angle
		// Then link the "next" pointers
		TArray<TArray<int32>> OutgoingByNode;
		OutgoingByNode.SetNum(NumNodes);

		// Collect outgoing half-edges for each node
		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			const int32 Origin = HalfEdges[HEIdx].OriginNode;
			OutgoingByNode[Origin].Add(HEIdx);
		}

		// Sort each node's outgoing half-edges by angle (CCW order)
		for (int32 NodeIdx = 0; NodeIdx < NumNodes; ++NodeIdx)
		{
			TArray<int32>& Outgoing = OutgoingByNode[NodeIdx];
			if (Outgoing.Num() <= 1) { continue; }

			// Sort by angle (ascending = CCW order)
			Outgoing.Sort([this](const int32 A, const int32 B)
			{
				return HalfEdges[A].Angle < HalfEdges[B].Angle;
			});
		}

		// Step 3: Link "next" pointers
		// For half-edge (u → v), its "next" is the half-edge that comes after (v → u) in CCW order around v
		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			FHalfEdge& HE = HalfEdges[HEIdx];
			const int32 TargetNode = HE.TargetNode;
			const int32 TwinIdx = HE.TwinIndex;

			// Find where the twin (v → u) appears in v's sorted outgoing list
			const TArray<int32>& TargetOutgoing = OutgoingByNode[TargetNode];
			const int32 TwinPosInList = TargetOutgoing.Find(TwinIdx);

			if (TwinPosInList == INDEX_NONE)
			{
				// Should never happen in a valid graph
				HE.NextIndex = -1;
				continue;
			}

			// The "next" half-edge is the one AFTER the twin in CCW order
			// This gives us faces with interior on the LEFT (CCW traversal)
			const int32 NextPosInList = (TwinPosInList + 1) % TargetOutgoing.Num();
			HE.NextIndex = TargetOutgoing[NextPosInList];
		}

		NumFaces = 0;
		bRawFacesEnumerated = false;
		CachedRawFaces.Reset();
	}

	void FPlanarFaceEnumerator::Build(const TSharedRef<FCluster>& InCluster, const TSharedPtr<TArray<FQuat>>& InNodeTangentFrames)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::Build::LocalTangent);

		Cluster = &InCluster.Get();
		NodeTangentFrames = InNodeTangentFrames;
		ProjectedPositions = nullptr;
		bIsLocalTangent = true;

		const TArray<FNode>& Nodes = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;
		PCGEx::FIndexLookup* NodeLookup = Cluster->NodeIndexLookup.Get();
		const int32 NumEdges = Edges.Num();
		const int32 NumNodes = Nodes.Num();
		const TArray<FQuat>& Frames = *NodeTangentFrames;

		// Step 1: Create all half-edges with angles computed in origin node's local tangent frame
		HalfEdges.Reset();
		HalfEdges.Reserve(NumEdges * 2);
		HalfEdgeMap.Reset();
		HalfEdgeMap.Reserve(NumEdges * 2);

		for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; ++EdgeIdx)
		{
			const PCGExGraphs::FEdge& Edge = Edges[EdgeIdx];
			const int32 NodeA = NodeLookup->Get(Edge.Start);
			const int32 NodeB = NodeLookup->Get(Edge.End);

			const FVector PosA = Cluster->GetPos(NodeA);
			const FVector PosB = Cluster->GetPos(NodeB);
			const FVector EdgeDir3D = (PosB - PosA).GetSafeNormal();

			// Half-edge A → B: project into NodeA's local frame
			{
				const FVector LocalDir = Frames[NodeA].UnrotateVector(EdgeDir3D);
				const double AngleAB = FMath::Atan2(LocalDir.Y, LocalDir.X);
				const int32 IndexAB = HalfEdges.Num();
				HalfEdges.Emplace(NodeA, NodeB, AngleAB);
				HalfEdgeMap.Add(PCGEx::H64(NodeA, NodeB), IndexAB);
			}

			// Half-edge B → A: project into NodeB's local frame
			{
				const FVector LocalDir = Frames[NodeB].UnrotateVector(-EdgeDir3D);
				const double AngleBA = FMath::Atan2(LocalDir.Y, LocalDir.X);
				const int32 IndexBA = HalfEdges.Num();
				HalfEdges.Emplace(NodeB, NodeA, AngleBA);
				HalfEdgeMap.Add(PCGEx::H64(NodeB, NodeA), IndexBA);
			}

			// Link twins
			const int32 IndexAB = HalfEdges.Num() - 2;
			const int32 IndexBA = HalfEdges.Num() - 1;
			HalfEdges[IndexAB].TwinIndex = IndexBA;
			HalfEdges[IndexBA].TwinIndex = IndexAB;
		}

		// Step 2: Collect and sort outgoing half-edges by angle per node (same as global projection)
		TArray<TArray<int32>> OutgoingByNode;
		OutgoingByNode.SetNum(NumNodes);

		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			OutgoingByNode[HalfEdges[HEIdx].OriginNode].Add(HEIdx);
		}

		for (int32 NodeIdx = 0; NodeIdx < NumNodes; ++NodeIdx)
		{
			TArray<int32>& Outgoing = OutgoingByNode[NodeIdx];
			if (Outgoing.Num() <= 1) { continue; }

			Outgoing.Sort([this](const int32 A, const int32 B)
			{
				return HalfEdges[A].Angle < HalfEdges[B].Angle;
			});
		}

		// Step 3: Link "next" pointers (identical logic — topology is topology)
		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			FHalfEdge& HE = HalfEdges[HEIdx];
			const int32 TargetNode = HE.TargetNode;
			const int32 TwinIdx = HE.TwinIndex;

			const TArray<int32>& TargetOutgoing = OutgoingByNode[TargetNode];
			const int32 TwinPosInList = TargetOutgoing.Find(TwinIdx);

			if (TwinPosInList == INDEX_NONE)
			{
				HE.NextIndex = -1;
				continue;
			}

			const int32 NextPosInList = (TwinPosInList + 1) % TargetOutgoing.Num();
			HE.NextIndex = TargetOutgoing[NextPosInList];
		}

		NumFaces = 0;
		bRawFacesEnumerated = false;
		CachedRawFaces.Reset();
	}

	const TArray<FRawFace>& FPlanarFaceEnumerator::EnumerateRawFaces()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::EnumerateRawFaces);

		if (bRawFacesEnumerated) { return CachedRawFaces; }
		if (!IsBuilt()) { return CachedRawFaces; }

		bRawFacesEnumerated = true;

		// Mark all half-edges as unvisited
		TArray<bool> Visited;
		Visited.SetNumZeroed(HalfEdges.Num());

		NumFaces = 0;

		// Enumerate faces by following "next" pointers
		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			if (Visited[StartHE]) { continue; }

			FRawFace& RawFace = CachedRawFaces.Emplace_GetRef(NumFaces);
			RawFace.Nodes.Reserve(64);

			int32 CurrentHE = StartHE;
			const int32 MaxSteps = HalfEdges.Num();

			for (int32 Step = 0; Step < MaxSteps; ++Step)
			{
				if (CurrentHE < 0 || CurrentHE >= HalfEdges.Num())
				{
					RawFace.Nodes.Reset();
					break;
				}

				if (Visited[CurrentHE])
				{
					if (CurrentHE != StartHE)
					{
						RawFace.Nodes.Reset();
					}
					break;
				}

				Visited[CurrentHE] = true;
				RawFace.Nodes.Add(HalfEdges[CurrentHE].OriginNode);
				HalfEdges[CurrentHE].FaceIndex = NumFaces;

				CurrentHE = HalfEdges[CurrentHE].NextIndex;
			}

			if (RawFace.Nodes.Num() >= 3)
			{
				NumFaces++;
			}
			else
			{
				CachedRawFaces.Pop();
			}
		}

		// Compute 3D bounds for each face (for early culling in bounded operations)
		for (FRawFace& RawFace : CachedRawFaces)
		{
			RawFace.Bounds3D = FBox(ForceInit);
			for (const int32 NodeIdx : RawFace.Nodes)
			{
				RawFace.Bounds3D += Cluster->GetPos(NodeIdx);
			}
		}

		return CachedRawFaces;
	}

	ECellResult FPlanarFaceEnumerator::BuildCellFromRawFace(
		const FRawFace& InRawFace,
		TSharedPtr<FCell>& OutCell,
		const TSharedRef<FCellConstraints>& Constraints) const
	{
		return BuildCellFromFace(InRawFace.Nodes, OutCell, Constraints);
	}

	void FPlanarFaceEnumerator::EnumerateAllFaces(TArray<TSharedPtr<FCell>>& OutCells, const TSharedRef<FCellConstraints>& Constraints, TArray<TSharedPtr<FCell>>* OutFailedCells, bool bDetectWrapper)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::EnumerateAllFaces);

		const TArray<FRawFace>& RawFaces = EnumerateRawFaces();
		const int32 NumRawFaces = RawFaces.Num();

		if (NumRawFaces == 0) { return; }

		// For small face counts, serial is faster due to parallel overhead
		constexpr int32 ParallelThreshold = 32;

		// Wrapper detection:
		// Normal/BestFit: CCW face (inverted winding) with largest area = unbounded exterior
		// LocalTangent: no topological exterior — wrapper = largest-area cell (heuristic)
		auto IsWrapperCandidate = [this](const FCell& Cell) -> bool
		{
			return bIsLocalTangent ? true : !Cell.Data.bIsClockwise;
		};

		if (NumRawFaces < ParallelThreshold)
		{
			// Serial path for small counts
			OutCells.Reserve(OutCells.Num() + NumRawFaces);
			double WrapperArea = -MAX_dbl;

			for (const FRawFace& RawFace : RawFaces)
			{
				TSharedPtr<FCell> Cell = MakeShared<FCell>(Constraints);
				const ECellResult Result = BuildCellFromRawFace(RawFace, Cell, Constraints);

				// Always set FaceIndex for adjacency lookups
				Cell->FaceIndex = RawFace.FaceIndex;

				if (Result == ECellResult::Success)
				{
					if (bDetectWrapper && IsWrapperCandidate(*Cell) && Cell->Data.Area > WrapperArea)
					{
						// Move previous wrapper candidate back to output if any
						if (Constraints->WrapperCell)
						{
							OutCells.Add(Constraints->WrapperCell);
						}
						Constraints->WrapperCell = Cell;
						WrapperArea = Cell->Data.Area;
					}
					else
					{
						OutCells.Add(Cell);
					}
				}
				else if (OutFailedCells && !Cell->Polygon.IsEmpty())
				{
					OutFailedCells->Add(Cell);
				}
			}
			return;
		}

		// Parallel path for larger counts
		// Pre-allocate result arrays - each slot corresponds to a raw face index
		TArray<TSharedPtr<FCell>> SuccessCells;
		SuccessCells.SetNum(NumRawFaces);

		TArray<TSharedPtr<FCell>> FailedCells;
		if (OutFailedCells) { FailedCells.SetNum(NumRawFaces); }

		// Process cells in parallel - each thread writes to its own index (no contention)
		ParallelFor(NumRawFaces, [&](const int32 RawFaceIdx)
		{
			const FRawFace& RawFace = RawFaces[RawFaceIdx];
			TSharedPtr<FCell> Cell = MakeShared<FCell>(Constraints);
			const ECellResult Result = BuildCellFromRawFace(RawFace, Cell, Constraints);

			// Always set FaceIndex for adjacency lookups
			Cell->FaceIndex = RawFace.FaceIndex;

			if (Result == ECellResult::Success)
			{
				SuccessCells[RawFaceIdx] = Cell;
			}
			else if (OutFailedCells && !Cell->Polygon.IsEmpty())
			{
				FailedCells[RawFaceIdx] = Cell;
			}
		});

		// Compact results - remove null entries, detect wrapper during compaction
		OutCells.Reserve(OutCells.Num() + NumRawFaces);
		double WrapperArea = -MAX_dbl;

		for (TSharedPtr<FCell>& Cell : SuccessCells)
		{
			if (!Cell) { continue; }

			if (bDetectWrapper && IsWrapperCandidate(*Cell) && Cell->Data.Area > WrapperArea)
			{
				if (Constraints->WrapperCell)
				{
					OutCells.Add(MoveTemp(Constraints->WrapperCell));
				}
				Constraints->WrapperCell = MoveTemp(Cell);
				WrapperArea = Constraints->WrapperCell->Data.Area;
			}
			else
			{
				OutCells.Add(MoveTemp(Cell));
			}
		}

		if (OutFailedCells)
		{
			OutFailedCells->Reserve(OutFailedCells->Num() + NumRawFaces);
			for (TSharedPtr<FCell>& Cell : FailedCells)
			{
				if (Cell) { OutFailedCells->Add(MoveTemp(Cell)); }
			}
		}
	}

	void FPlanarFaceEnumerator::EnumerateFacesWithinBounds(
		TArray<TSharedPtr<FCell>>& OutCells,
		const TSharedRef<FCellConstraints>& Constraints,
		const FBox& BoundsFilter,
		bool bIncludeOutside,
		TArray<TSharedPtr<FCell>>* OutFailedCells,
		bool bDetectWrapper)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::EnumerateFacesWithinBounds);

		const TArray<FRawFace>& RawFaces = EnumerateRawFaces();
		const int32 NumRawFaces = RawFaces.Num();

		if (NumRawFaces == 0) { return; }

		// Early exit if no culling possible (include outside or invalid bounds)
		if (bIncludeOutside || !BoundsFilter.IsValid)
		{
			EnumerateAllFaces(OutCells, Constraints, OutFailedCells, bDetectWrapper);
			return;
		}

		// Count faces that pass bounds filter (for reserve sizing)
		int32 PotentialCount = 0;
		for (const FRawFace& RawFace : RawFaces)
		{
			if (BoundsFilter.Intersect(RawFace.Bounds3D)) { PotentialCount++; }
		}

		// Wrapper detection lambda (same logic as EnumerateAllFaces)
		auto IsWrapperCandidate = [this](const FCell& Cell) -> bool
		{
			return bIsLocalTangent ? true : !Cell.Data.bIsClockwise;
		};

		// For small face counts, serial is faster due to parallel overhead
		constexpr int32 ParallelThreshold = 32;

		if (PotentialCount < ParallelThreshold)
		{
			// Serial path
			OutCells.Reserve(OutCells.Num() + PotentialCount);
			double WrapperArea = -MAX_dbl;

			for (const FRawFace& RawFace : RawFaces)
			{
				// EARLY CULLING: Skip faces whose bounds don't intersect filter
				if (!BoundsFilter.Intersect(RawFace.Bounds3D)) { continue; }

				TSharedPtr<FCell> Cell = MakeShared<FCell>(Constraints);
				const ECellResult Result = BuildCellFromRawFace(RawFace, Cell, Constraints);

				// Always set FaceIndex for adjacency lookups
				Cell->FaceIndex = RawFace.FaceIndex;

				if (Result == ECellResult::Success)
				{
					if (bDetectWrapper && IsWrapperCandidate(*Cell) && Cell->Data.Area > WrapperArea)
					{
						// Move previous wrapper candidate back to output if any
						if (Constraints->WrapperCell)
						{
							OutCells.Add(Constraints->WrapperCell);
						}
						Constraints->WrapperCell = Cell;
						WrapperArea = Cell->Data.Area;
					}
					else
					{
						OutCells.Add(Cell);
					}
				}
				else if (OutFailedCells && !Cell->Polygon.IsEmpty())
				{
					OutFailedCells->Add(Cell);
				}
			}
			return;
		}

		// Parallel path for larger counts
		// Pre-allocate result arrays - each slot corresponds to a raw face index
		TArray<TSharedPtr<FCell>> SuccessCells;
		SuccessCells.SetNum(NumRawFaces);

		TArray<TSharedPtr<FCell>> FailedCells;
		if (OutFailedCells) { FailedCells.SetNum(NumRawFaces); }

		// Process cells in parallel - each thread writes to its own index (no contention)
		// Early culling happens inside the parallel loop
		ParallelFor(NumRawFaces, [&](const int32 RawFaceIdx)
		{
			const FRawFace& RawFace = RawFaces[RawFaceIdx];

			// EARLY CULLING: Skip faces whose bounds don't intersect filter
			if (!BoundsFilter.Intersect(RawFace.Bounds3D)) { return; }

			TSharedPtr<FCell> Cell = MakeShared<FCell>(Constraints);
			const ECellResult Result = BuildCellFromRawFace(RawFace, Cell, Constraints);

			// Always set FaceIndex for adjacency lookups
			Cell->FaceIndex = RawFace.FaceIndex;

			if (Result == ECellResult::Success)
			{
				SuccessCells[RawFaceIdx] = Cell;
			}
			else if (OutFailedCells && !Cell->Polygon.IsEmpty())
			{
				FailedCells[RawFaceIdx] = Cell;
			}
		});

		// Compact results - remove null entries, detect wrapper during compaction
		OutCells.Reserve(OutCells.Num() + PotentialCount);
		double WrapperArea = -MAX_dbl;

		for (TSharedPtr<FCell>& Cell : SuccessCells)
		{
			if (!Cell) { continue; }

			if (bDetectWrapper && IsWrapperCandidate(*Cell) && Cell->Data.Area > WrapperArea)
			{
				if (Constraints->WrapperCell)
				{
					OutCells.Add(MoveTemp(Constraints->WrapperCell));
				}
				Constraints->WrapperCell = MoveTemp(Cell);
				WrapperArea = Constraints->WrapperCell->Data.Area;
			}
			else
			{
				OutCells.Add(MoveTemp(Cell));
			}
		}

		if (OutFailedCells)
		{
			OutFailedCells->Reserve(OutFailedCells->Num() + PotentialCount);
			for (TSharedPtr<FCell>& Cell : FailedCells)
			{
				if (Cell) { OutFailedCells->Add(MoveTemp(Cell)); }
			}
		}
	}

	ECellResult FPlanarFaceEnumerator::BuildCellFromFace(
		const TArray<int32>& FaceNodes,
		TSharedPtr<FCell>& OutCell,
		const TSharedRef<FCellConstraints>& Constraints) const
	{
		const int32 NumUniqueNodes = FaceNodes.Num();
		if (NumUniqueNodes < 3) { return ECellResult::Leaf; }

		// Check point count limits (based on unique nodes)
		if (NumUniqueNodes < Constraints->MinPointCount || NumUniqueNodes > Constraints->MaxPointCount)
		{
			return ECellResult::OutsidePointsLimit;
		}

		// Build nodes array with leaf duplication support
		OutCell->Nodes.Reset();
		OutCell->Nodes.Reserve(NumUniqueNodes * 2); // Reserve extra for potential leaf duplicates

		OutCell->Data.Bounds = FBox(ForceInit);
		OutCell->Data.Centroid = FVector::ZeroVector;

		double Perimeter = 0;
		int32 Sign = 0;
		FVector PrevPos = Cluster->GetPos(FaceNodes.Last());

		for (int32 i = 0; i < NumUniqueNodes; ++i)
		{
			const int32 NodeIdx = FaceNodes[i];
			const FNode& Node = (*Cluster->Nodes)[NodeIdx];
			const bool bIsLeaf = Node.IsLeaf();

			// Check for leaves
			if (bIsLeaf && !Constraints->bKeepCellsWithLeaves)
			{
				return ECellResult::Leaf;
			}

			// Add node (and duplicate if leaf and duplication is enabled)
			OutCell->Nodes.Add(NodeIdx);
			if (bIsLeaf && Constraints->bDuplicateLeafPoints)
			{
				OutCell->Nodes.Add(NodeIdx);
			}

			const FVector Pos = Cluster->GetPos(NodeIdx);

			OutCell->Data.Bounds += Pos;
			OutCell->Data.Centroid += Pos;

			// Compute segment length
			const double SegmentLength = FVector::Dist(PrevPos, Pos);
			Perimeter += SegmentLength;
			PrevPos = Pos;

			if (SegmentLength < Constraints->MinSegmentLength || SegmentLength > Constraints->MaxSegmentLength)
			{
				return ECellResult::OutsideSegmentsLimit;
			}

			// Check convexity
			if (i >= 2)
			{
				PCGExMath::CheckConvex(
					Cluster->GetPos(FaceNodes[i - 2]),
					Cluster->GetPos(FaceNodes[i - 1]),
					Pos,
					OutCell->Data.bIsConvex,
					Sign);

				if (Constraints->bConvexOnly && !OutCell->Data.bIsConvex)
				{
					return ECellResult::WrongAspect;
				}
			}
		}

		// Normalize nodes for hash computation
		PCGExArrayHelpers::ShiftArrayToSmallest(OutCell->Nodes);

		// Check for duplicate
		if (!Constraints->IsUniqueCellHash(OutCell)) { return ECellResult::Duplicate; }

		OutCell->Data.Centroid /= NumUniqueNodes;
		OutCell->Data.Perimeter = Perimeter;
		OutCell->Data.bIsClosedLoop = true;

		// Check bounds size
		const double BoundsSize = OutCell->Data.Bounds.GetSize().Length();
		if (BoundsSize < Constraints->MinBoundsSize || BoundsSize > Constraints->MaxBoundsSize)
		{
			return ECellResult::OutsideBoundsLimit;
		}

		// Check perimeter limits
		if (Perimeter < Constraints->MinPerimeter || Perimeter > Constraints->MaxPerimeter)
		{
			return ECellResult::OutsidePerimeterLimit;
		}

		// Build polygon from the expanded nodes array (includes leaf duplicates)
		const int32 NumOutputNodes = OutCell->Nodes.Num();
		OutCell->Polygon.SetNumUninitialized(NumOutputNodes);
		OutCell->Bounds2D = FBox2D(ForceInit);

		// Per-face local projection for LocalTangent (computed once, used for polygon + holes)
		FPCGExGeo2DProjectionDetails FaceProjection;

		if (bIsLocalTangent)
		{
			// Compute best-fit plane from the face nodes' 3D positions
			PCGExMath::FBestFitPlane FacePlane(NumUniqueNodes,
				[&](int32 i) { return Cluster->GetPos(FaceNodes[i]); });
			FaceProjection.Init(FacePlane);

			for (int32 i = 0; i < NumOutputNodes; ++i)
			{
				const FVector Projected = FaceProjection.Project(Cluster->GetPos(OutCell->Nodes[i]));
				OutCell->Polygon[i] = FVector2D(Projected.X, Projected.Y);
				OutCell->Bounds2D += OutCell->Polygon[i];
			}
		}
		else
		{
			// Existing global projection path
			// Note: ProjectedPositions is node-indexed, access directly via NodeIdx
			for (int32 i = 0; i < NumOutputNodes; ++i)
			{
				const int32 NodeIdx = OutCell->Nodes[i];
				const FVector2D& Point = (*ProjectedPositions)[NodeIdx];
				OutCell->Polygon[i] = Point;
				OutCell->Bounds2D += Point;
			}
		}

		// Compute polygon properties (area, winding, compactness)
		PCGExMath::FPolygonInfos PolyInfos = PCGExMath::FPolygonInfos(OutCell->Polygon);
		OutCell->Data.Area = PolyInfos.Area * 0.01; // QoL scaling
		OutCell->Data.bIsClockwise = PolyInfos.bIsClockwise;
		OutCell->Data.Compactness = PolyInfos.Compactness;

		// Fix winding if needed
		if (!PolyInfos.IsWinded(Constraints->Winding))
		{
			Algo::Reverse(OutCell->Nodes);
			Algo::Reverse(OutCell->Polygon);
		}

		// Check holes
		if (Constraints->Holes)
		{
			if (bIsLocalTangent)
			{
				if (Constraints->Holes->OverlapsPolygonLocal(
					OutCell->Polygon, OutCell->Bounds2D, OutCell->Data.Bounds, FaceProjection))
				{
					return ECellResult::Hole;
				}
			}
			else
			{
				if (Constraints->Holes->OverlapsPolygon(OutCell->Polygon, OutCell->Bounds2D))
				{
					return ECellResult::Hole;
				}
			}
		}

		// Check compactness limits
		if (OutCell->Data.Compactness < Constraints->MinCompactness ||
			OutCell->Data.Compactness > Constraints->MaxCompactness)
		{
			return ECellResult::OutsideCompactnessLimit;
		}

		// Check area limits
		if (OutCell->Data.Area < Constraints->MinArea || OutCell->Data.Area > Constraints->MaxArea)
		{
			return ECellResult::OutsideAreaLimit;
		}

		// Check concave constraint
		if (Constraints->bConcaveOnly && OutCell->Data.bIsConvex)
		{
			return ECellResult::WrongAspect;
		}

		OutCell->bBuiltSuccessfully = true;
		return ECellResult::Success;
	}

	int32 FPlanarFaceEnumerator::FindFaceContaining(const FVector2D& Point) const
	{
		// LocalTangent: 2D point query is meaningless (no global 2D space)
		if (bIsLocalTangent) { return -1; }

		// Simple point-in-polygon test for each face
		// This could be optimized with spatial indexing
		TArray<FVector2D> FacePolygon;

		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			const int32 FaceIdx = HalfEdges[StartHE].FaceIndex;
			if (FaceIdx < 0) { continue; }

			// Check if we've already tested this face
			bool bAlreadyTested = false;
			for (int32 PrevHE = 0; PrevHE < StartHE; ++PrevHE)
			{
				if (HalfEdges[PrevHE].FaceIndex == FaceIdx)
				{
					bAlreadyTested = true;
					break;
				}
			}
			if (bAlreadyTested) { continue; }

			// Build face polygon (ProjectedPositions is node-indexed)
			FacePolygon.Reset();
			int32 CurrentHE = StartHE;
			const int32 MaxSteps = HalfEdges.Num();

			for (int32 Step = 0; Step < MaxSteps; ++Step)
			{
				const FHalfEdge& HE = HalfEdges[CurrentHE];
				FacePolygon.Add((*ProjectedPositions)[HE.OriginNode]);
				CurrentHE = HE.NextIndex;
				if (CurrentHE == StartHE) { break; }
			}

			if (FacePolygon.Num() >= 3 && PCGExMath::Geo::IsPointInPolygon(Point, FacePolygon))
			{
				return FaceIdx;
			}
		}

		return -1;
	}

	TMap<int32, TSet<int32>> FPlanarFaceEnumerator::BuildCellAdjacencyMap(int32 WrapperFaceIndex) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::BuildCellAdjacencyMap);

		TMap<int32, TSet<int32>> AdjacencyMap;

		if (!bRawFacesEnumerated || HalfEdges.IsEmpty()) { return AdjacencyMap; }

		// Reserve space for the map
		AdjacencyMap.Reserve(NumFaces);

		// Iterate through all half-edges
		for (const FHalfEdge& HE : HalfEdges)
		{
			const int32 FaceA = HE.FaceIndex;
			if (FaceA < 0 || FaceA == WrapperFaceIndex) { continue; }

			// Get the twin's face
			if (HE.TwinIndex < 0 || HE.TwinIndex >= HalfEdges.Num()) { continue; }
			const int32 FaceB = HalfEdges[HE.TwinIndex].FaceIndex;

			// Skip if same face, invalid, or wrapper
			if (FaceB < 0 || FaceB == FaceA || FaceB == WrapperFaceIndex) { continue; }

			// Add bidirectional adjacency
			AdjacencyMap.FindOrAdd(FaceA).Add(FaceB);
			AdjacencyMap.FindOrAdd(FaceB).Add(FaceA);
		}

		return AdjacencyMap;
	}

	const TMap<int32, TSet<int32>>& FPlanarFaceEnumerator::GetOrBuildAdjacencyMap(int32 WrapperFaceIndex) const
	{
		// Fast path: check if already cached with same wrapper index
		{
			FRWScopeLock ReadLock(AdjacencyMapLock, SLT_ReadOnly);
			if (bAdjacencyMapCached && CachedAdjacencyWrapperIndex == WrapperFaceIndex)
			{
				return CachedAdjacencyMap;
			}
		}

		// Slow path: need to build or rebuild
		{
			FRWScopeLock WriteLock(AdjacencyMapLock, SLT_Write);

			// Double-check after acquiring write lock
			if (bAdjacencyMapCached && CachedAdjacencyWrapperIndex == WrapperFaceIndex)
			{
				return CachedAdjacencyMap;
			}

			CachedAdjacencyMap = BuildCellAdjacencyMap(WrapperFaceIndex);
			CachedAdjacencyWrapperIndex = WrapperFaceIndex;
			bAdjacencyMapCached = true;
		}

		return CachedAdjacencyMap;
	}

	void FPlanarFaceEnumerator::GetAdjacentFaces(int32 FaceIndex, TArray<int32>& OutAdjacentFaces, int32 WrapperFaceIndex) const
	{
		OutAdjacentFaces.Reset();

		if (!bRawFacesEnumerated || FaceIndex < 0 || HalfEdges.IsEmpty()) { return; }

		TSet<int32> UniqueAdjacent;

		// Find all half-edges belonging to this face and check their twins
		for (const FHalfEdge& HE : HalfEdges)
		{
			if (HE.FaceIndex != FaceIndex) { continue; }

			// Get the twin's face
			if (HE.TwinIndex < 0 || HE.TwinIndex >= HalfEdges.Num()) { continue; }
			const int32 AdjacentFace = HalfEdges[HE.TwinIndex].FaceIndex;

			// Skip if invalid or wrapper
			if (AdjacentFace < 0 || AdjacentFace == WrapperFaceIndex) { continue; }

			UniqueAdjacent.Add(AdjacentFace);
		}

		OutAdjacentFaces = UniqueAdjacent.Array();
	}

	void FPlanarFaceEnumerator::GetFaceHalfEdges(int32 FaceIndex, TArray<int32>& OutHalfEdgeIndices) const
	{
		OutHalfEdgeIndices.Reset();

		if (!bRawFacesEnumerated || FaceIndex < 0 || HalfEdges.IsEmpty()) { return; }

		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			if (HalfEdges[HEIdx].FaceIndex == FaceIndex)
			{
				OutHalfEdgeIndices.Add(HEIdx);
			}
		}
	}

	int32 FPlanarFaceEnumerator::GetWrapperFaceIndex() const
	{
		// LocalTangent: closed manifolds have no unbounded exterior face
		if (bIsLocalTangent) { return -1; }

		// The wrapper face is the one with the largest (most negative for CCW) signed area
		// or equivalently the face that would have CW winding when all others have CCW
		double LargestArea = -MAX_dbl;
		int32 WrapperIdx = -1;

		TArray<FVector2D> FacePolygon;
		TSet<int32> ProcessedFaces;

		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			const int32 FaceIdx = HalfEdges[StartHE].FaceIndex;
			if (FaceIdx < 0 || ProcessedFaces.Contains(FaceIdx)) { continue; }
			ProcessedFaces.Add(FaceIdx);

			// Build face polygon (ProjectedPositions is node-indexed)
			FacePolygon.Reset();
			int32 CurrentHE = StartHE;
			const int32 MaxSteps = HalfEdges.Num();

			for (int32 Step = 0; Step < MaxSteps; ++Step)
			{
				const FHalfEdge& HE = HalfEdges[CurrentHE];
				FacePolygon.Add((*ProjectedPositions)[HE.OriginNode]);
				CurrentHE = HE.NextIndex;
				if (CurrentHE == StartHE) { break; }
			}

			if (FacePolygon.Num() >= 3)
			{
				// Compute signed area - wrapper will have opposite sign
				double SignedArea = 0;
				for (int32 i = 0; i < FacePolygon.Num(); ++i)
				{
					const FVector2D& P1 = FacePolygon[i];
					const FVector2D& P2 = FacePolygon[(i + 1) % FacePolygon.Num()];
					SignedArea += (P1.X * P2.Y - P2.X * P1.Y);
				}
				SignedArea *= 0.5;

				// The wrapper face will have the largest absolute area
				const double AbsArea = FMath::Abs(SignedArea);
				if (AbsArea > LargestArea)
				{
					LargestArea = AbsArea;
					WrapperIdx = FaceIdx;
				}
			}
		}

		return WrapperIdx;
	}
}
