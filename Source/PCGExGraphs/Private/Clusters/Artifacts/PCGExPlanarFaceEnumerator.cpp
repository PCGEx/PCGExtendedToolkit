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

		const TArray<FEdge>& Edges = *InCluster->Edges;
		PCGEx::FIndexLookup* NodeLookup = InCluster->NodeIndexLookup.Get();

		// Edge.Start and Edge.End are POINT indices, convert to node indices for the core build
		TArray<uint64> NodeEdges;
		NodeEdges.Reserve(Edges.Num());
		for (const FEdge& Edge : Edges)
		{
			NodeEdges.Add(PCGEx::H64(NodeLookup->Get(Edge.Start), NodeLookup->Get(Edge.End)));
		}

		Build(InCluster->Nodes->Num(), NodeEdges, InNodeIndexedPositions);
		Cluster = &InCluster.Get();
	}

	void FPlanarFaceEnumerator::Build(
		const int32 InNumNodes,
		TConstArrayView<uint64> InEdges,
		const TSharedPtr<TArray<FVector2D>>& InNodeIndexedPositions,
		TConstArrayView<FVector> InNodePositions3D)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::Build);

		Cluster = nullptr;
		StandalonePositions3D = TArray<FVector>(InNodePositions3D);
		StandaloneNumNodes = InNumNodes;
		ProjectedPositions = InNodeIndexedPositions;
		NodeTangentFrames = nullptr;
		bIsLocalTangent = false;

		const int32 NumEdges = InEdges.Num();
		const int32 NumNodes = InNumNodes;
		const TArray<FVector2D>& Positions = *ProjectedPositions;

		// Step 1: Create all half-edges (2 per edge)
		HalfEdges.Reset();
		HalfEdges.Reserve(NumEdges * 2);
		HalfEdgeMap.Reset();
		HalfEdgeMap.Reserve(NumEdges * 2);

		for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; ++EdgeIdx)
		{
			const int32 NodeA = PCGEx::H64A(InEdges[EdgeIdx]);
			const int32 NodeB = PCGEx::H64B(InEdges[EdgeIdx]);

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
			if (Outgoing.Num() <= 1)
			{
				continue;
			}

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

	void FPlanarFaceEnumerator::Build(const int32 InNumNodes, TConstArrayView<uint64> InEdges, TConstArrayView<FVector> InNodePositions3D)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::Build::StandaloneLocalTangent);

		Cluster = nullptr;
		StandalonePositions3D = TArray<FVector>(InNodePositions3D);
		StandaloneNumNodes = InNumNodes;
		ProjectedPositions = nullptr;
		NodeTangentFrames = nullptr;
		bIsLocalTangent = true;

		// Half-edges + twins only: the two-phase LocalTangent enumeration computes its own successor
		// choices, so no angles and no next-linking are needed here.
		const int32 NumEdges = InEdges.Num();
		HalfEdges.Reset();
		HalfEdges.Reserve(NumEdges * 2);
		HalfEdgeMap.Reset();
		HalfEdgeMap.Reserve(NumEdges * 2);

		for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; ++EdgeIdx)
		{
			const int32 NodeA = PCGEx::H64A(InEdges[EdgeIdx]);
			const int32 NodeB = PCGEx::H64B(InEdges[EdgeIdx]);

			const int32 IndexAB = HalfEdges.Num();
			HalfEdges.Emplace(NodeA, NodeB, 0);
			HalfEdgeMap.Add(PCGEx::H64(NodeA, NodeB), IndexAB);

			const int32 IndexBA = HalfEdges.Num();
			HalfEdges.Emplace(NodeB, NodeA, 0);
			HalfEdgeMap.Add(PCGEx::H64(NodeB, NodeA), IndexBA);

			HalfEdges[IndexAB].TwinIndex = IndexBA;
			HalfEdges[IndexBA].TwinIndex = IndexAB;
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
		StandalonePositions3D.Reset();
		bIsLocalTangent = true;

		const TArray<FNode>& Nodes = *Cluster->Nodes;
		const TArray<FEdge>& Edges = *Cluster->Edges;
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
			const FEdge& Edge = Edges[EdgeIdx];
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
			if (Outgoing.Num() <= 1)
			{
				continue;
			}

			Outgoing.Sort([this](const int32 A, const int32 B)
			{
				return HalfEdges[A].Angle < HalfEdges[B].Angle;
			});
		}

		// Step 3: Link "next" pointers (identical logic -- topology is topology)
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

		if (bRawFacesEnumerated)
		{
			return CachedRawFaces;
		}
		if (!IsBuilt())
		{
			return CachedRawFaces;
		}

		bRawFacesEnumerated = true;

		if (bIsLocalTangent)
		{
			// Static per-node frames cannot order edges at a CREASE (a fold's floor and wall edges either
			// collapse onto one projected ray or project to zero length). Two phases instead:
			// 1. Exact planar-patch faces -- maximal coplanar regions each run the proven PLANAR path,
			//    which nails piecewise-planar complexes (floors + walls) deterministically.
			// 2. A parallel-transported walk over whatever remains (the outer face, curved regions).
			// Both write NextIndex, keeping downstream DCEL consumers (polygon walks, region tracing) coherent.
			TArray<bool> Visited;
			Visited.SetNumZeroed(HalfEdges.Num());
			NumFaces = 0;
			EnumeratePlanarPatchFaces(Visited);
			EnumerateRawFacesTransported(Visited);
		}
		else
		{
			// Mark all half-edges as unvisited
			TArray<bool> Visited;
			Visited.SetNumZeroed(HalfEdges.Num());

			NumFaces = 0;

			// Enumerate faces by following "next" pointers
			for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
			{
				if (Visited[StartHE])
				{
					continue;
				}

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
		}

		// Compute 3D bounds for each face (for early culling in bounded operations)
		for (FRawFace& RawFace : CachedRawFaces)
		{
			RawFace.Bounds3D = FBox(ForceInit);
			for (const int32 NodeIdx : RawFace.Nodes)
			{
				RawFace.Bounds3D += GetNodePos3D(NodeIdx);
			}
		}

		return CachedRawFaces;
	}

	FVector FPlanarFaceEnumerator::GetNodePos3D(const int32 NodeIdx) const
	{
		if (Cluster) { return Cluster->GetPos(NodeIdx); }
		if (StandalonePositions3D.IsValidIndex(NodeIdx)) { return StandalonePositions3D[NodeIdx]; }
		const FVector2D& Pos2D = (*ProjectedPositions)[NodeIdx];
		return FVector(Pos2D.X, Pos2D.Y, 0);
	}

	int32 FPlanarFaceEnumerator::GetNumNodes() const
	{
		return Cluster ? Cluster->Nodes->Num() : StandaloneNumNodes;
	}

	void FPlanarFaceEnumerator::EnumeratePlanarPatchFaces(TArray<bool>& VisitedHalfEdges)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::EnumeratePlanarPatchFaces);

		const int32 NumNodes = GetNumNodes();
		const TArray<FQuat>* Frames = NodeTangentFrames.Get();

		// Undirected edge list + per-node incidence, from the half-edges themselves
		struct FUEdge
		{
			int32 A = -1;
			int32 B = -1;
		};
		TArray<FUEdge> UEdges;
		UEdges.Reserve(HalfEdges.Num() / 2);
		TArray<TArray<int32>> NodeUEdges;
		NodeUEdges.SetNum(NumNodes);

		FBox NodeBounds(ForceInit);
		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			const FHalfEdge& HE = HalfEdges[HEIdx];
			if (HE.OriginNode < HE.TargetNode)
			{
				const int32 UIdx = UEdges.Add({HE.OriginNode, HE.TargetNode});
				NodeUEdges[HE.OriginNode].Add(UIdx);
				NodeUEdges[HE.TargetNode].Add(UIdx);
			}
			NodeBounds += GetNodePos3D(HE.OriginNode);
		}

		const int32 NumUEdges = UEdges.Num();
		if (NumUEdges < 3)
		{
			return;
		}

		// Coplanarity tolerance, scaled to the cluster so huge and tiny sketches behave alike
		const double PlaneTol = FMath::Max(NodeBounds.GetSize().GetMax() * 1.0e-4, 0.01);

		TArray<bool> EdgeSeeded;
		EdgeSeeded.SetNumZeroed(NumUEdges);
		TArray<bool> InPatch; // scratch, reused per growth
		InPatch.SetNumZeroed(NumUEdges);

		auto EdgeDir = [&](const FUEdge& E) { return (GetNodePos3D(E.B) - GetNodePos3D(E.A)).GetSafeNormal(); };

		// Grow the maximal set of edges coplanar with (PlaneOrigin, PlaneNormal), starting from SeedEdge.
		TArray<int32> NodeQueue; // scratch, reused per growth
		auto GrowPatch = [&](const int32 SeedEdge, const FVector& PlaneOrigin, const FVector& PlaneNormal, TArray<int32>& OutEdges)
		{
			OutEdges.Reset();
			NodeQueue.Reset();
			auto OnPlane = [&](const int32 NodeIdx) { return FMath::Abs(FVector::DotProduct(GetNodePos3D(NodeIdx) - PlaneOrigin, PlaneNormal)) <= PlaneTol; };

			auto Absorb = [&](const int32 UIdx)
			{
				InPatch[UIdx] = true;
				OutEdges.Add(UIdx);
				NodeQueue.Add(UEdges[UIdx].A);
				NodeQueue.Add(UEdges[UIdx].B);
			};

			Absorb(SeedEdge);
			for (int32 QueueIdx = 0; QueueIdx < NodeQueue.Num(); ++QueueIdx)
			{
				for (const int32 UIdx : NodeUEdges[NodeQueue[QueueIdx]])
				{
					if (InPatch[UIdx])
					{
						continue;
					}
					if (OnPlane(UEdges[UIdx].A) && OnPlane(UEdges[UIdx].B))
					{
						Absorb(UIdx);
					}
				}
			}

			for (const int32 UIdx : OutEdges)
			{
				InPatch[UIdx] = false; // reset scratch
			}
		};

		TArray<int32> PatchEdges;
		TArray<int32> BestPatchEdges;

		// Shared across every patch iteration: the throwaway enumerator releases its buffer reference at
		// the end of each Build cycle, so overwriting is safe and capacity is retained throughout.
		const TSharedPtr<TArray<FVector2D>> PatchPositions = MakeShared<TArray<FVector2D>>();
		FPlanarFaceEnumerator PatchEnum;
		TMap<int32, int32> GlobalToLocal;
		TArray<int32> LocalToGlobal;
		TArray<uint64> PatchEdgePairs;

		for (int32 SeedEdge = 0; SeedEdge < NumUEdges; ++SeedEdge)
		{
			if (EdgeSeeded[SeedEdge])
			{
				continue;
			}

			// Best plane through this edge: try every independent partner at either endpoint, keep the
			// plane absorbing the most edges (a bogus diagonal plane absorbs almost nothing).
			const FUEdge& Seed = UEdges[SeedEdge];
			const FVector SeedDir = EdgeDir(Seed);
			const FVector SeedOrigin = GetNodePos3D(Seed.A);

			FVector BestNormal = FVector::ZeroVector;
			BestPatchEdges.Reset();

			for (const int32 EndNode : {Seed.A, Seed.B})
			{
				for (const int32 PartnerIdx : NodeUEdges[EndNode])
				{
					if (PartnerIdx == SeedEdge)
					{
						continue;
					}
					const FVector Cross = FVector::CrossProduct(SeedDir, EdgeDir(UEdges[PartnerIdx]));
					if (Cross.SizeSquared() <= UE_DOUBLE_KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const FVector PlaneNormal = Cross.GetSafeNormal();
					GrowPatch(SeedEdge, SeedOrigin, PlaneNormal, PatchEdges);
					if (PatchEdges.Num() > BestPatchEdges.Num())
					{
						Swap(BestPatchEdges, PatchEdges); // GrowPatch resets its output anyway
						BestNormal = PlaneNormal;
					}
				}
			}

			// Whatever the outcome, this edge had its shot -- the transported walk owns leftovers.
			for (const int32 UIdx : BestPatchEdges)
			{
				EdgeSeeded[UIdx] = true;
			}
			EdgeSeeded[SeedEdge] = true;

			if (BestPatchEdges.Num() < 3)
			{
				continue;
			}

			// Sign-align the patch plane with the (BFS-consistent) node frames, so every patch winds with
			// the same global orientation and the two half-edges of a fold land in different patches' faces.
			if (Frames)
			{
				FVector FrameSum = FVector::ZeroVector;
				for (const int32 UIdx : BestPatchEdges)
				{
					FrameSum += (*Frames)[UEdges[UIdx].A].GetAxisZ();
					FrameSum += (*Frames)[UEdges[UIdx].B].GetAxisZ();
				}
				if (FVector::DotProduct(BestNormal, FrameSum) < 0)
				{
					BestNormal = -BestNormal;
				}
			}

			// Project the patch and run the exact PLANAR enumeration on it (cluster-free build)
			FVector BasisX, BasisY;
			BestNormal.FindBestAxisVectors(BasisX, BasisY);
			// (X, Y, n) right-handed so CCW-in-basis = CCW about the aligned normal
			BasisY = FVector::CrossProduct(BestNormal, BasisX);

			// The throwaway build works in a COMPACT patch-local index space: sizing anything to the full
			// cluster node count would make this phase O(Patches x NumNodes) instead of O(total edges).
			GlobalToLocal.Reset();
			LocalToGlobal.Reset();
			PatchEdgePairs.Reset();
			PatchEdgePairs.Reserve(BestPatchEdges.Num());

			auto LocalIdx = [&](const int32 GlobalIdx)
			{
				if (const int32* Found = GlobalToLocal.Find(GlobalIdx))
				{
					return *Found;
				}
				const int32 Local = LocalToGlobal.Add(GlobalIdx);
				GlobalToLocal.Add(GlobalIdx, Local);
				return Local;
			};

			for (const int32 UIdx : BestPatchEdges)
			{
				const FUEdge& E = UEdges[UIdx];
				PatchEdgePairs.Add(PCGEx::H64(LocalIdx(E.A), LocalIdx(E.B)));
			}

			PatchPositions->SetNumUninitialized(LocalToGlobal.Num(), EAllowShrinking::No);
			for (int32 Local = 0; Local < LocalToGlobal.Num(); ++Local)
			{
				const FVector Rel = GetNodePos3D(LocalToGlobal[Local]) - SeedOrigin;
				(*PatchPositions)[Local] = FVector2D(FVector::DotProduct(Rel, BasisX), FVector::DotProduct(Rel, BasisY));
			}

			PatchEnum.Build(LocalToGlobal.Num(), PatchEdgePairs, PatchPositions);
			const TArray<FRawFace>& PatchFaces = PatchEnum.EnumerateRawFaces();
			const int32 PatchWrapper = PatchEnum.GetWrapperFaceIndex();

			for (const FRawFace& PatchFace : PatchFaces)
			{
				if (PatchFace.FaceIndex == PatchWrapper || PatchFace.Nodes.Num() < 3)
				{
					continue;
				}

				// Back to GLOBAL node ids, then map the cycle onto global half-edges; on a claim collision
				// try the reversed winding (safety net for orientation drift), else skip -- another patch
				// owns those half-edges.
				TArray<int32> Cycle;
				Cycle.Reserve(PatchFace.Nodes.Num());
				for (const int32 Local : PatchFace.Nodes)
				{
					Cycle.Add(LocalToGlobal[Local]);
				}
				bool bClaimable = false;
				for (int32 Attempt = 0; Attempt < 2 && !bClaimable; ++Attempt)
				{
					bClaimable = true;
					for (int32 i = 0; i < Cycle.Num(); ++i)
					{
						const int32* HEIdx = HalfEdgeMap.Find(PCGEx::H64(Cycle[i], Cycle[(i + 1) % Cycle.Num()]));
						if (!HEIdx || VisitedHalfEdges[*HEIdx])
						{
							bClaimable = false;
							break;
						}
					}
					if (!bClaimable && Attempt == 0)
					{
						Algo::Reverse(Cycle);
					}
				}
				if (!bClaimable)
				{
					continue;
				}

				FRawFace& GlobalFace = CachedRawFaces.Emplace_GetRef(NumFaces);
				GlobalFace.Nodes = Cycle;
				for (int32 i = 0; i < Cycle.Num(); ++i)
				{
					const int32 HEIdx = HalfEdgeMap.FindChecked(PCGEx::H64(Cycle[i], Cycle[(i + 1) % Cycle.Num()]));
					const int32 NextHEIdx = HalfEdgeMap.FindChecked(PCGEx::H64(Cycle[(i + 1) % Cycle.Num()], Cycle[(i + 2) % Cycle.Num()]));
					VisitedHalfEdges[HEIdx] = true;
					HalfEdges[HEIdx].FaceIndex = NumFaces;
					HalfEdges[HEIdx].NextIndex = NextHEIdx;
				}
				NumFaces++;
			}
		}
	}

	void FPlanarFaceEnumerator::EnumerateRawFacesTransported(TArray<bool>& VisitedHalfEdges)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::EnumerateRawFacesTransported);

		const int32 NumNodes = GetNumNodes();

		// Outgoing half-edges per node (Build's local copy is long gone)
		TArray<TArray<int32>> OutgoingByNode;
		OutgoingByNode.SetNum(NumNodes);
		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			OutgoingByNode[HalfEdges[HEIdx].OriginNode].Add(HEIdx);
		}

		TArray<bool>& Visited = VisitedHalfEdges;
		const TArray<FQuat>* Frames = NodeTangentFrames.Get();

		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			if (Visited[StartHE])
			{
				continue;
			}

			FRawFace& RawFace = CachedRawFaces.Emplace_GetRef(NumFaces);
			RawFace.Nodes.Reserve(64);

			// The face's normal, seeded from the start node's tangent frame then CARRIED along the walk
			// (re-orthogonalized against each edge = parallel transport, which also follows curvature).
			FVector Normal = Frames ? (*Frames)[HalfEdges[StartHE].OriginNode].GetAxisZ() : FVector::UpVector;

			int32 CurrentHE = StartHE;
			const int32 MaxSteps = HalfEdges.Num();

			for (int32 Step = 0; Step < MaxSteps; ++Step)
			{
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

				const FHalfEdge& HE = HalfEdges[CurrentHE];
				const FVector Target = GetNodePos3D(HE.TargetNode);
				const FVector Dir = (Target - GetNodePos3D(HE.OriginNode)).GetSafeNormal();

				// Transport the normal across this edge
				if (!Dir.IsNearlyZero())
				{
					const FVector Transported = (Normal - FVector::DotProduct(Normal, Dir) * Dir).GetSafeNormal();
					if (!Transported.IsNearlyZero())
					{
						Normal = Transported;
					}
				}

				// The planar rule made crease-proof: successor = sharpest CCW turn from the back-direction,
				// measured in the TRANSPORTED plane. An edge perpendicular to that plane (the other side of
				// a fold) has no azimuth here and belongs to another face's walk.
				const FVector Ref = -Dir;
				const TArray<int32>& Outgoing = OutgoingByNode[HE.TargetNode];

				int32 BestHE = INDEX_NONE;
				double BestAngle = TNumericLimits<double>::Max();
				int32 FallbackHE = INDEX_NONE;
				double FallbackDot = TNumericLimits<double>::Max();

				for (const int32 CandHE : Outgoing)
				{
					// A half-edge already claimed by another face is off-limits -- EXCEPT the walk's own
					// start, which is how a cycle closes. This is what routes the outer walk up a wall
					// instead of onto a fold half-edge the planar patches already own.
					if (Visited[CandHE] && CandHE != StartHE)
					{
						continue;
					}

					// The twin is a dead-end bounce, legal only when nothing else leaves the node
					if (CandHE == HE.TwinIndex && Outgoing.Num() > 1)
					{
						continue;
					}

					const FVector CandDir = (GetNodePos3D(HalfEdges[CandHE].TargetNode) - Target).GetSafeNormal();
					const double NormalDot = FVector::DotProduct(CandDir, Normal);
					const FVector InPlane = CandDir - NormalDot * Normal;
					const double InPlaneLen = InPlane.Size();

					if (const double AbsDot = FMath::Abs(NormalDot); AbsDot < FallbackDot)
					{
						FallbackDot = AbsDot;
						FallbackHE = CandHE;
					}

					if (InPlaneLen <= UE_DOUBLE_KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const FVector Projected = InPlane / InPlaneLen;
					double Angle = FMath::Atan2(FVector::DotProduct(Normal, FVector::CrossProduct(Ref, Projected)), FVector::DotProduct(Ref, Projected));
					if (Angle <= UE_DOUBLE_KINDA_SMALL_NUMBER)
					{
						Angle += UE_DOUBLE_TWO_PI; // continuing straight back reads as a full turn
					}

					if (Angle < BestAngle)
					{
						BestAngle = Angle;
						BestHE = CandHE;
					}
				}

				if (BestHE == INDEX_NONE)
				{
					// Every departure is perpendicular to the carried plane (or only the twin exists):
					// take the least out-of-plane one rather than stalling the walk.
					BestHE = FallbackHE != INDEX_NONE ? FallbackHE : HE.TwinIndex;
				}

				HalfEdges[CurrentHE].NextIndex = BestHE;
				CurrentHE = BestHE;
				if (CurrentHE == StartHE)
				{
					break;
				}
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

		if (!ensureMsgf(Cluster, TEXT("Cell building requires a cluster-built enumerator")))
		{
			return;
		}

		const TArray<FRawFace>& RawFaces = EnumerateRawFaces();
		const int32 NumRawFaces = RawFaces.Num();

		if (NumRawFaces == 0)
		{
			return;
		}

		// For small face counts, serial is faster due to parallel overhead
		constexpr int32 ParallelThreshold = 32;

		// Wrapper detection:
		// Normal/BestFit: CCW face (inverted winding) with largest area = unbounded exterior
		// LocalTangent: no topological exterior -- wrapper = largest-area cell (heuristic)
		auto IsWrapperCandidate = [this](const FCell& Cell) -> bool
		{
			return bIsLocalTangent ? true : !Cell.Data.bIsClockwise;
		};

		if (NumRawFaces < ParallelThreshold)
		{
			// Serial path for small counts
			OutCells.Reserve(OutCells.Num() + NumRawFaces);
			double WrapperArea = TNumericLimits<double>::Lowest();

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
		if (OutFailedCells)
		{
			FailedCells.SetNum(NumRawFaces);
		}

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
		double WrapperArea = TNumericLimits<double>::Lowest();

		for (TSharedPtr<FCell>& Cell : SuccessCells)
		{
			if (!Cell)
			{
				continue;
			}

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
				if (Cell)
				{
					OutFailedCells->Add(MoveTemp(Cell));
				}
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

		if (!ensureMsgf(Cluster, TEXT("Cell building requires a cluster-built enumerator")))
		{
			return;
		}

		const TArray<FRawFace>& RawFaces = EnumerateRawFaces();
		const int32 NumRawFaces = RawFaces.Num();

		if (NumRawFaces == 0)
		{
			return;
		}

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
			if (BoundsFilter.Intersect(RawFace.Bounds3D))
			{
				PotentialCount++;
			}
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
			double WrapperArea = TNumericLimits<double>::Lowest();

			for (const FRawFace& RawFace : RawFaces)
			{
				// EARLY CULLING: Skip faces whose bounds don't intersect filter
				if (!BoundsFilter.Intersect(RawFace.Bounds3D))
				{
					continue;
				}

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
		if (OutFailedCells)
		{
			FailedCells.SetNum(NumRawFaces);
		}

		// Process cells in parallel - each thread writes to its own index (no contention)
		// Early culling happens inside the parallel loop
		ParallelFor(NumRawFaces, [&](const int32 RawFaceIdx)
		{
			const FRawFace& RawFace = RawFaces[RawFaceIdx];

			// EARLY CULLING: Skip faces whose bounds don't intersect filter
			if (!BoundsFilter.Intersect(RawFace.Bounds3D))
			{
				return;
			}

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
		double WrapperArea = TNumericLimits<double>::Lowest();

		for (TSharedPtr<FCell>& Cell : SuccessCells)
		{
			if (!Cell)
			{
				continue;
			}

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
				if (Cell)
				{
					OutFailedCells->Add(MoveTemp(Cell));
				}
			}
		}
	}

	ECellResult FPlanarFaceEnumerator::BuildCellFromFace(
		const TArray<int32>& FaceNodes,
		TSharedPtr<FCell>& OutCell,
		const TSharedRef<FCellConstraints>& Constraints) const
	{
		// Cell building reads cluster nodes for FCell metrics — cluster-free builds only support face enumeration/queries
		if (!ensureMsgf(Cluster, TEXT("Cell building requires a cluster-built enumerator")))
		{
			return ECellResult::MalformedCluster;
		}

		const int32 NumUniqueNodes = FaceNodes.Num();
		if (NumUniqueNodes < 3)
		{
			return ECellResult::Leaf;
		}

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

		if (!Constraints->IsUniqueCellHash(OutCell))
		{
			return ECellResult::Duplicate;
		}

		OutCell->Data.Centroid /= NumUniqueNodes;
		OutCell->Data.Perimeter = Perimeter;
		OutCell->Data.bIsClosedLoop = true;

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

		// Per-face best-fit plane: LocalTangent polygons live in it; planar builds only pay for it when a
		// consumer opted into distance-to-plane gating (bComputeFacePlanes) -- unread otherwise. The fit
		// reuses the centroid computed above and skips extents (PlaneOnly); the plane's Z in the rotated
		// frame is just the rotated centroid's Z (rotation is linear).
		FPCGExGeo2DProjectionDetails FaceProjection;
		if (bIsLocalTangent || Constraints->bComputeFacePlanes)
		{
			const PCGExMath::FBestFitPlane FacePlane = PCGExMath::FBestFitPlane::PlaneOnly(
				NumUniqueNodes,
				[&](const int32 i) { return Cluster->GetPos(FaceNodes[i]); },
				OutCell->Data.Centroid);
			FaceProjection.Init(FacePlane);

			OutCell->FacePlaneQuat = FaceProjection.ProjectionQuat;
			OutCell->FacePlaneZ = FaceProjection.Project(OutCell->Data.Centroid).Z;
			OutCell->bHasFacePlane = true;
			OutCell->bPolygonInFaceFrame = bIsLocalTangent;
		}

		if (bIsLocalTangent)
		{
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

		if (Constraints->bConcaveOnly && OutCell->Data.bIsConvex)
		{
			return ECellResult::WrongAspect;
		}

		// Infer Seed from first two distinct consecutive nodes
		for (int32 i = 0; i < OutCell->Nodes.Num() - 1; ++i)
		{
			if (OutCell->Nodes[i] != OutCell->Nodes[i + 1])
			{
				OutCell->Seed = FLink(OutCell->Nodes[i], Cluster->GetNode(OutCell->Nodes[i])->GetEdgeIndex(OutCell->Nodes[i + 1]));
				break;
			}
		}

		OutCell->bBuiltSuccessfully = true;
		return ECellResult::Success;
	}

	int32 FPlanarFaceEnumerator::FindFaceContaining(const FVector2D& Point) const
	{
		// LocalTangent: 2D point query is meaningless (no global 2D space)
		if (bIsLocalTangent)
		{
			return -1;
		}

		// Simple point-in-polygon test for each face
		// This could be optimized with spatial indexing
		TArray<FVector2D> FacePolygon;
		TSet<int32> ProcessedFaces;

		// Containment test is orientation-independent, so the wrapper's polygon contains every
		// point inside the hull -- it must be excluded or it shadows the interior faces.
		const int32 WrapperFaceIdx = GetWrapperFaceIndex();

		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			const int32 FaceIdx = HalfEdges[StartHE].FaceIndex;
			if (FaceIdx < 0 || FaceIdx == WrapperFaceIdx || ProcessedFaces.Contains(FaceIdx))
			{
				continue;
			}
			ProcessedFaces.Add(FaceIdx);

			BuildFacePolygonFrom(StartHE, FacePolygon);

			if (FacePolygon.Num() >= 3 && PCGExMath::Geo::IsPointInPolygon(Point, FacePolygon))
			{
				return FaceIdx;
			}
		}

		return -1;
	}

	void FPlanarFaceEnumerator::GetFacePolygon(const int32 FaceIndex, TArray<FVector2D>& OutPolygon) const
	{
		OutPolygon.Reset();

		if (bIsLocalTangent || FaceIndex < 0)
		{
			return;
		}

		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			if (HalfEdges[HEIdx].FaceIndex == FaceIndex)
			{
				BuildFacePolygonFrom(HEIdx, OutPolygon);
				return;
			}
		}
	}

	void FPlanarFaceEnumerator::BuildFacePolygonFrom(const int32 StartHalfEdge, TArray<FVector2D>& OutPolygon) const
	{
		// ProjectedPositions is node-indexed
		OutPolygon.Reset();

		int32 CurrentHE = StartHalfEdge;
		const int32 MaxSteps = HalfEdges.Num();

		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			const FHalfEdge& HE = HalfEdges[CurrentHE];
			OutPolygon.Add((*ProjectedPositions)[HE.OriginNode]);
			CurrentHE = HE.NextIndex;
			if (CurrentHE == StartHalfEdge)
			{
				break;
			}
		}
	}

	TMap<int32, TSet<int32>> FPlanarFaceEnumerator::BuildCellAdjacencyMap(int32 WrapperFaceIndex) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::BuildCellAdjacencyMap);

		TMap<int32, TSet<int32>> AdjacencyMap;

		if (!bRawFacesEnumerated || HalfEdges.IsEmpty())
		{
			return AdjacencyMap;
		}

		AdjacencyMap.Reserve(NumFaces);

		for (const FHalfEdge& HE : HalfEdges)
		{
			const int32 FaceA = HE.FaceIndex;
			if (FaceA < 0 || FaceA == WrapperFaceIndex)
			{
				continue;
			}

			if (HE.TwinIndex < 0 || HE.TwinIndex >= HalfEdges.Num())
			{
				continue;
			}
			const int32 FaceB = HalfEdges[HE.TwinIndex].FaceIndex;

			// Skip if same face, invalid, or wrapper
			if (FaceB < 0 || FaceB == FaceA || FaceB == WrapperFaceIndex)
			{
				continue;
			}

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

		if (!bRawFacesEnumerated || FaceIndex < 0 || HalfEdges.IsEmpty())
		{
			return;
		}

		TSet<int32> UniqueAdjacent;

		// Find all half-edges belonging to this face and check their twins
		for (const FHalfEdge& HE : HalfEdges)
		{
			if (HE.FaceIndex != FaceIndex)
			{
				continue;
			}

			if (HE.TwinIndex < 0 || HE.TwinIndex >= HalfEdges.Num())
			{
				continue;
			}
			const int32 AdjacentFace = HalfEdges[HE.TwinIndex].FaceIndex;

			if (AdjacentFace < 0 || AdjacentFace == WrapperFaceIndex)
			{
				continue;
			}

			UniqueAdjacent.Add(AdjacentFace);
		}

		OutAdjacentFaces = UniqueAdjacent.Array();
	}

	void FPlanarFaceEnumerator::GetFaceHalfEdges(int32 FaceIndex, TArray<int32>& OutHalfEdgeIndices) const
	{
		OutHalfEdgeIndices.Reset();

		if (!bRawFacesEnumerated || FaceIndex < 0 || HalfEdges.IsEmpty())
		{
			return;
		}

		for (int32 HEIdx = 0; HEIdx < HalfEdges.Num(); ++HEIdx)
		{
			if (HalfEdges[HEIdx].FaceIndex == FaceIndex)
			{
				OutHalfEdgeIndices.Add(HEIdx);
			}
		}
	}

	void FPlanarFaceEnumerator::GetSharedSegments(TArray<FSharedSegment>& OutSegments, int32 WrapperFaceIndex) const
	{
		OutSegments.Reset();

		if (!bRawFacesEnumerated || HalfEdges.IsEmpty())
		{
			return;
		}

		const int32 NumHalfEdges = HalfEdges.Num();

		// Visit each undirected segment once (h < twin). Mirrors the twin walk in BuildCellAdjacencyMap,
		// but keeps the segment endpoints so callers (e.g. midpoint vertices) don't re-walk the DCEL.
		for (int32 h = 0; h < NumHalfEdges; ++h)
		{
			const FHalfEdge& HE = HalfEdges[h];
			const int32 Twin = HE.TwinIndex;
			if (Twin < 0 || Twin >= NumHalfEdges || h > Twin)
			{
				continue;
			}

			const int32 FaceA = HE.FaceIndex;
			const int32 FaceB = HalfEdges[Twin].FaceIndex;
			if (FaceA < 0 || FaceB < 0 || FaceA == FaceB || FaceA == WrapperFaceIndex || FaceB == WrapperFaceIndex)
			{
				continue;
			}

			OutSegments.Emplace(FSharedSegment{HE.OriginNode, HE.TargetNode, FaceA, FaceB});
		}
	}

	void FPlanarFaceEnumerator::TraceRegionBoundaries(const TSet<int32>& InFaceSet, TArray<TArray<int32>>& OutLoops) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPlanarFaceEnumerator::TraceRegionBoundaries);

		OutLoops.Reset();

		const int32 NumHalfEdges = HalfEdges.Num();
		if (NumHalfEdges == 0 || InFaceSet.IsEmpty())
		{
			return;
		}

		auto IsInSet = [&InFaceSet](const int32 FaceIndex) -> bool
		{
			return FaceIndex >= 0 && InFaceSet.Contains(FaceIndex);
		};

		// Boundary half-edge: own face in the region, twin's face outside it (region on its left).
		auto IsBoundary = [&](const int32 HEIndex) -> bool
		{
			const FHalfEdge& HE = HalfEdges[HEIndex];
			if (!IsInSet(HE.FaceIndex))
			{
				return false;
			}
			const int32 Twin = HE.TwinIndex;
			return Twin < 0 || Twin >= NumHalfEdges || !IsInSet(HalfEdges[Twin].FaceIndex);
		};

		TArray<bool> Visited;
		Visited.SetNumZeroed(NumHalfEdges);

		for (int32 StartHE = 0; StartHE < NumHalfEdges; ++StartHE)
		{
			if (Visited[StartHE] || !IsBoundary(StartHE))
			{
				continue;
			}

			TArray<int32> Loop;
			int32 Current = StartHE;
			bool bValid = true;
			int32 Steps = 0;

			while (true)
			{
				if (Visited[Current])
				{
					// A well-formed loop closes by returning to its start half-edge.
					bValid = (Current == StartHE);
					break;
				}

				Visited[Current] = true;
				Loop.Add(HalfEdges[Current].OriginNode);

				// Next boundary half-edge out of the target vertex: rotate through the in-set face fan (twin+Next)
				// until an edge whose right side leaves the region. Correct even when a vertex is visited twice.
				int32 Next = HalfEdges[Current].NextIndex;
				int32 Turns = 0;
				while (Next >= 0 && Next < NumHalfEdges)
				{
					const int32 NextTwin = HalfEdges[Next].TwinIndex;
					if (NextTwin < 0 || NextTwin >= NumHalfEdges || !IsInSet(HalfEdges[NextTwin].FaceIndex))
					{
						break; // Next is a boundary half-edge -> the loop's next segment
					}
					Next = HalfEdges[NextTwin].NextIndex; // interior edge -> cross into the neighbour and keep turning
					if (++Turns > NumHalfEdges)
					{
						Next = -1;
						break;
					}
				}

				if (Next < 0 || Next >= NumHalfEdges)
				{
					bValid = false;
					break;
				}

				Current = Next;
				if (++Steps > NumHalfEdges)
				{
					bValid = false;
					break;
				}
			}

			if (bValid && Loop.Num() >= 3)
			{
				OutLoops.Emplace(MoveTemp(Loop));
			}
		}
	}

	int32 FPlanarFaceEnumerator::GetWrapperFaceIndex() const
	{
		// LocalTangent: closed manifolds have no unbounded exterior face
		if (bIsLocalTangent)
		{
			return -1;
		}

		// The wrapper face is the one with the largest (most negative for CCW) signed area
		// or equivalently the face that would have CW winding when all others have CCW
		double LargestArea = TNumericLimits<double>::Lowest();
		int32 WrapperIdx = -1;

		TArray<FVector2D> FacePolygon;
		TSet<int32> ProcessedFaces;

		for (int32 StartHE = 0; StartHE < HalfEdges.Num(); ++StartHE)
		{
			const int32 FaceIdx = HalfEdges[StartHE].FaceIndex;
			if (FaceIdx < 0 || ProcessedFaces.Contains(FaceIdx))
			{
				continue;
			}
			ProcessedFaces.Add(FaceIdx);

			BuildFacePolygonFrom(StartHE, FacePolygon);

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
