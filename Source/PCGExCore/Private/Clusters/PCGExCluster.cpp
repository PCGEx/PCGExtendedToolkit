// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCache.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "HAL/PlatformAtomics.h"
#include "Math/PCGExMathAxis.h"

namespace PCGExClusters
{
	FCluster::FCluster(const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TSharedPtr<PCGEx::FIndexLookup>& InNodeIndexLookup)
		: NodeIndexLookup(InNodeIndexLookup)
		  , VtxIO(InVtxIO)
		  , EdgesIO(InEdgesIO)
	{
		Nodes = MakeShared<TArray<FNode>>();
		Edges = MakeShared<TArray<FEdge>>();
		Bounds = FBox(ForceInit);

		VtxPoints = InVtxIO->GetIn();
	}

	// "Mirror" constructor: creates a cluster that shares or copies structure from another.
	// Used when multiple operations need independent cluster state (e.g. different edge validity)
	// but share the same underlying point/edge data. When bCopyNodes/bCopyEdges are false,
	// the arrays are shared (not duplicated), so structural reads are zero-cost.
	FCluster::FCluster(const TSharedRef<FCluster>& OtherCluster, const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TSharedPtr<PCGEx::FIndexLookup>& InNodeIndexLookup, const bool bCopyNodes, const bool bCopyEdges, const bool bCopyLookup)
		: NodeIndexLookup(InNodeIndexLookup)
		  , VtxIO(InVtxIO)
		  , EdgesIO(InEdgesIO)
	{
		NodeIndexLookup = InNodeIndexLookup;
		VtxPoints = InVtxIO->GetIn();
		VtxTransforms = VtxPoints->GetConstTransformValueRange();

		bIsMirror = true;
		OriginalCluster = OtherCluster;

		NumRawVtx = InVtxIO->GetNum();
		NumRawEdges = InEdgesIO->GetNum();

		Bounds = OriginalCluster->Bounds;

		BoundedEdges = OriginalCluster->BoundedEdges;

		if (bCopyNodes)
		{
			const int32 NumNewNodes = OriginalCluster->Nodes->Num();

			Nodes = MakeShared<TArray<FNode>>();
			TArray<FNode>& NewNodes = *Nodes;
			TArray<FNode>& SourceNodes = *OriginalCluster->Nodes;

			PCGExArrayHelpers::InitArray(NewNodes, NumNewNodes);

			for (int i = 0; i < NumNewNodes; i++)
			{
				const FNode& NewNode = (NewNodes[i] = SourceNodes[i]);

				// Update index lookup
				NodeIndexLookup->GetMutable(NewNode.PointIndex) = NewNode.Index;
			}

			NodesDataPtr = Nodes->GetData();
		}
		else
		{
			Nodes = OriginalCluster->Nodes;
			NodesDataPtr = OriginalCluster->NodesDataPtr;

			// Update index lookup
			for (const FNode& Node : *Nodes)
			{
				NodeIndexLookup->GetMutable(Node.PointIndex) = Node.Index;
			}
		}

		if (bCopyEdges)
		{
			const int32 NumNewEdges = OriginalCluster->Edges->Num();

			Edges = MakeShared<TArray<FEdge>>();
			TArray<FEdge>& NewEdges = *Edges;
			TArray<FEdge>& SourceEdges = *OriginalCluster->Edges;

			PCGExArrayHelpers::InitArray(NewEdges, NumNewEdges);

			const int32 EdgeIOIndex = InEdgesIO->IOIndex;
			for (int i = 0; i < NumNewEdges; i++)
			{
				const FEdge& SourceEdge = SourceEdges[i];
				FEdge& NewEdge = (NewEdges[i] = SourceEdge);
				NewEdge.IOIndex = EdgeIOIndex;
			}

			BoundedEdges.Reset();
			EdgesDataPtr = Edges->GetData();
		}
		else
		{
			Edges = OriginalCluster->Edges;
			EdgesDataPtr = OriginalCluster->EdgesDataPtr;
		}
	}

	void FCluster::TConstVtxLookup::Dump(TArray<int32>& OutIndices) const
	{
		const int32 NumNodes = Num();
		OutIndices.SetNum(NumNodes);
		for (int i = 0; i < NumNodes; i++)
		{
			OutIndices[i] = NodesArray[i].PointIndex;
		}
	}

	void FCluster::TVtxLookup::Dump(TArray<int32>& OutIndices) const
	{
		const int32 NumNodes = Num();
		OutIndices.SetNum(NumNodes);
		for (int i = 0; i < NumNodes; i++)
		{
			OutIndices[i] = NodesArray[i].PointIndex;
		}
	}

	void FCluster::ClearInheritedForChanges(const bool bClearOwned)
	{
		WillModifyVtxIO(bClearOwned);
		WillModifyVtxPositions(bClearOwned);
	}

	void FCluster::WillModifyVtxIO(const bool bClearOwned)
	{
	}

	void FCluster::WillModifyVtxPositions(const bool bClearOwned)
	{
		NodeOctree.Reset();
		EdgeOctree.Reset();
		BoundedEdges.Reset();
		EdgeLengths.Reset();
		MinEdgeLength = 0;
		MaxEdgeLength = 0;
		ClearCachedData();
	}

	FCluster::~FCluster()
	{
	}

	bool FCluster::BuildFrom(const TMap<uint32, int32>& InEndpointsLookup, const TArray<int32>* InExpectedAdjacency)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCluster::BuildCluster);

		const TSharedPtr<PCGExData::FPointIO> PinnedVtxIO = VtxIO.Pin();
		const TSharedPtr<PCGExData::FPointIO> PinnedEdgesIO = EdgesIO.Pin();
		const int32 EdgeIOIndex = PinnedEdgesIO ? PinnedEdgesIO->IOIndex : -1;

		if (!PinnedVtxIO || !PinnedEdgesIO)
		{
			return false;
		}

		const UPCGBasePointData* InNodePoints = PinnedVtxIO->GetIn();
		VtxTransforms = InNodePoints->GetConstTransformValueRange();

		Nodes->Empty();
		Edges->Empty();

		// Each edge stores its two endpoint vertex indices packed into a single int64.
		// The EndpointsLookup maps vertex hash → point index to resolve edges.
		const TUniquePtr<PCGExData::TArrayBuffer<int64>> EndpointsBuffer = MakeUnique<PCGExData::TArrayBuffer<int64>>(PinnedEdgesIO.ToSharedRef(), Labels::Attr_PCGExEdgeIdx);
		if (!EndpointsBuffer->InitForRead())
		{
			return false;
		}

		NumRawVtx = InNodePoints->GetNumPoints();
		NumRawEdges = PinnedEdgesIO->GetNum();

		auto OnFail = [&]()
		{
			Nodes->Empty();
			Edges->Empty();
			return false;
		};

		const int32 NumEdges = PinnedEdgesIO->GetNum();

		PCGExArrayHelpers::InitArray(Edges, NumEdges);

		const TArray<int64>& Endpoints = *EndpointsBuffer->GetInValues().Get();

		for (int i = 0; i < NumEdges; i++)
		{
			// Unpack the two vertex hashes from the int64 edge descriptor.
			uint32 A;
			uint32 B;
			PCGEx::H64(Endpoints[i], A, B);

			const int32* StartPointIndexPtr = InEndpointsLookup.Find(A);
			const int32* EndPointIndexPtr = InEndpointsLookup.Find(B);

			// Reject edges with missing endpoints or self-loops.
			if ((!StartPointIndexPtr || !EndPointIndexPtr || *StartPointIndexPtr == *EndPointIndexPtr))
			{
				return OnFail();
			}

			*(Edges->GetData() + i) = FEdge(i, *StartPointIndexPtr, *EndPointIndexPtr, i, EdgeIOIndex);
		}

		// Nodes are a subset of all vtx points - only those referenced by at least one
		// edge get a node, so NumRawVtx is only an upper bound on the node count.
		BuildAdjacency_Unsafe(NumRawVtx);

		// Validate against expected adjacency counts (from a previous cluster build).
		// Only checks for missing connections, not extra ones, to detect broken edges.
		if (InExpectedAdjacency)
		{
			for (const FNode& Node : (*Nodes))
			{
				if ((*InExpectedAdjacency)[Node.PointIndex] > Node.Num())
				{
					return OnFail();
				}
			}
		}

		Bounds = Bounds.ExpandBy(10);

		NodesDataPtr = Nodes->GetData();
		EdgesDataPtr = Edges->GetData();

		return true;
	}

	void FCluster::BuildFromSubgraphData(const TSharedPtr<PCGExData::FFacade>& InVtxFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeFacade, const TArray<FEdge>& InEdges, const int32 InNumNodes)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCluster::BuildClusterFromSubgraph);

		TSharedPtr<FCluster> LocalPin = SharedThis(this);

		Bounds = FBox(ForceInit);

		NumRawVtx = InVtxFacade->Source->GetNum(PCGExData::EIOSide::Out);
		NumRawEdges = InEdgeFacade->Source->GetNum(PCGExData::EIOSide::Out);

		const UPCGBasePointData* SubVtxPoints = InVtxFacade->Source->GetOutIn();
		VtxTransforms = SubVtxPoints->GetConstTransformValueRange();

		Edges->Reserve(NumRawEdges);
		Edges->Append(InEdges);

		// The shared, -1-initialized lookup (created fresh per graph compile) doubles as
		// the point->node scratch: subgraphs partition vtx points (union-find components),
		// so concurrent cluster builds each claim a disjoint set of indices, and the values
		// written during creation ARE the lookup's final content. This replaces a per-cluster
		// TSparseArray whose expansion cost scaled with the highest point index touched.
		BuildAdjacency_Unsafe(InNumNodes);

		// Subgraph node counts are exact: every subgraph node is an endpoint of at
		// least one of its edges (see FGraph::BuildSubGraphs).
		check(Nodes->Num() == InNumNodes)

		Bounds = Bounds.ExpandBy(10);

		NodesDataPtr = Nodes->GetData();
		EdgesDataPtr = Edges->GetData();
	}

	void FCluster::BuildAdjacency_Unsafe(const int32 InNumNodesHint)
	{
		check(NodeIndexLookup)

		const TArray<FEdge>& EdgesRef = *Edges;
		const int32 NumEdges = EdgesRef.Num();

		if (NumEdges < ParallelClusterBuildThreshold)
		{
			Nodes->Reserve(InNumNodesHint);

			for (const FEdge& E : EdgesRef)
			{
				const int32 StartNode = GetOrCreateNode_Unsafe(E.Start);
				const int32 EndNode = GetOrCreateNode_Unsafe(E.End);

				(Nodes->GetData() + StartNode)->Link(EndNode, E.Index);
				(Nodes->GetData() + EndNode)->Link(StartNode, E.Index);
			}

			Nodes->Shrink();
			return;
		}

		TArray<FNode>& NodesRef = *Nodes;

		// Uninitialized on purpose: the assignment loop below placement-news each node
		// exactly once at first appearance, and the never-constructed tail is trimmed
		// with SetNumUnsafeInternal (no destructor runs on it).
		NodesRef.SetNumUninitialized(InNumNodesHint);
		FNode* NodesData = NodesRef.GetData();

		const FEdge* EdgesData = EdgesRef.GetData();
		PCGEx::FIndexLookup* Lookup = NodeIndexLookup.Get();

		// Per-node degree counts, gathered during assignment; reused as scatter cursors.
		TArray<int32> LinkCursors;
		LinkCursors.SetNumZeroed(InNumNodesHint);
		int32* Cursors = LinkCursors.GetData();

		// Sequential index assignment in first-appearance order -- matches the
		// sequential path's node ordering exactly, at two lookup probes and two plain
		// increments per edge. Everything heavy below runs parallel.
		int32 NumCreated = 0;

		auto AssignNode = [&](const uint32 PointIndex) -> int32
		{
			int32& NodeIndex = Lookup->GetMutable(PointIndex);
			if (NodeIndex == -1)
			{
				check(NumCreated < InNumNodesHint)
				new(NodesData + NumCreated) FNode(NumCreated, PointIndex);
				NodeIndex = NumCreated++;
			}
			else
			{
				// A non -1 entry must be a node this build created; anything else is a
				// stale/foreign lookup, which the contract forbids.
				checkSlow(NodeIndex >= 0 && NodeIndex < NumCreated && NodesData[NodeIndex].PointIndex == static_cast<int32>(PointIndex))
			}
			return NodeIndex;
		};

		for (int32 i = 0; i < NumEdges; i++)
		{
			const FEdge& E = EdgesData[i];
			Cursors[AssignNode(E.Start)]++;
			Cursors[AssignNode(E.End)]++;
		}

		// Trim the unconstructed tail without running destructors on it.
		NodesRef.SetNumUnsafeInternal(NumCreated);

		// Size each node's links to its exact degree and gather bounds. Link slots are
		// left uninitialized: the scatter below writes every one exactly once.
		FRWLock BoundsLock;
		PCGExMT::ParallelOrSequentialScoped(
			NumCreated,
			[&](const PCGExMT::FScope& Scope)
			{
				FBox ScopedBounds(ForceInit);
				PCGEX_SCOPE_LOOP(i)
				{
					FNode& Node = NodesData[i];
					Node.Links.SetNumUninitialized(Cursors[i]);
					ScopedBounds += VtxTransforms[Node.PointIndex].GetLocation();
				}

				FWriteScopeLock WriteLock(BoundsLock);
				Bounds += ScopedBounds;
			});

		// Scatter links; each atomic decrement claims a unique slot, so concurrent
		// writes into a node's link array never alias.
		PCGExMT::ParallelOrSequential(
			NumEdges,
			[&](const int32 i)
			{
				const FEdge& E = EdgesData[i];
				const int32 StartNode = Lookup->Get(E.Start);
				const int32 EndNode = Lookup->Get(E.End);

				NodesData[StartNode].Links[FPlatformAtomics::InterlockedDecrement(Cursors + StartNode)] = FLink(EndNode, E.Index);
				NodesData[EndNode].Links[FPlatformAtomics::InterlockedDecrement(Cursors + EndNode)] = FLink(StartNode, E.Index);
			});

		// Slot claim order is scheduling-dependent; sorting by edge index restores
		// the deterministic ascending order the sequential path produces.
		PCGExMT::ParallelOrSequential(
			NumCreated,
			[&](const int32 i)
			{
				NodesData[i].Links.Sort([](const FLink& A, const FLink& B) { return A.Edge < B.Edge; });
			});

		Nodes->Shrink();
	}

	bool FCluster::IsValidWith(const TSharedRef<PCGExData::FPointIO>& InVtxIO, const TSharedRef<PCGExData::FPointIO>& InEdgesIO) const
	{
		return NumRawVtx == InVtxIO->GetNum() && NumRawEdges == InEdgesIO->GetNum();
	}

	bool FCluster::HasTag(const FString& InTag)
	{
		if (const TSharedPtr<PCGExData::FPointIO>& PinnedVtxIO = VtxIO.Pin())
		{
			if (PinnedVtxIO->Tags->IsTagged(InTag))
			{
				return true;
			}
		}
		if (const TSharedPtr<PCGExData::FPointIO>& PinnedEdgesIO = EdgesIO.Pin())
		{
			if (PinnedEdgesIO->Tags->IsTagged(InTag))
			{
				return true;
			}
		}
		return false;
	}

	FNode* FCluster::GetGuidedHalfEdge(const int32 Edge, const FVector& Guide, const FVector& Up) const
	{
		// Returns the endpoint of an edge that is on the "Guide" side.
		// Used to determine directional traversal: which end of an edge
		// faces toward a guide point. Leaf nodes (degree 1) are preferred
		// as starting points since they're path endpoints.
		FNode* StartNode = GetEdgeStart(Edge);
		FNode* EndNode = GetEdgeEnd(Edge);

		if (StartNode->IsLeaf() && !EndNode->IsLeaf())
		{
			return StartNode;
		}
		if (EndNode->IsLeaf() && !StartNode->IsLeaf())
		{
			return EndNode;
		}

		// Project Guide onto the edge, then check which side of the edge's
		// normal plane the guide point falls on.
		const FVector& A = GetPos(StartNode);
		const FVector& B = GetPos(EndNode);
		const FVector& C = FMath::ClosestPointOnSegment(Guide, A, B);

		if (FVector::DotProduct((Guide - C).GetSafeNormal(), PCGExMath::GetNormalUp(A, B, Up)) < 0)
		{
			return GetEdgeStart(Edge);
		}
		return GetEdgeEnd(Edge);
	}

	double FCluster::EdgeDistToEdge(const FEdge* A, const FEdge* B, FVector& OutP1, FVector& OutP2) const
	{
		FMath::SegmentDistToSegment(GetStartPos(A), GetEndPos(A), GetStartPos(B), GetEndPos(B), OutP1, OutP2);

		return FVector::Dist(OutP1, OutP2);
	}

	double FCluster::EdgeDistToEdge(const int32 EdgeA, const int32 EdgeB, FVector& OutP1, FVector& OutP2) const
	{
		return EdgeDistToEdge(GetEdge(EdgeA), GetEdge(EdgeB), OutP1, OutP2);
	}

	double FCluster::EdgeDistToEdgeSquared(const FEdge* A, const FEdge* B, FVector& OutP1, FVector& OutP2) const
	{
		FMath::SegmentDistToSegment(GetStartPos(A), GetEndPos(A), GetStartPos(B), GetEndPos(B), OutP1, OutP2);

		return FVector::DistSquared(OutP1, OutP2);
	}

	double FCluster::EdgeDistToEdgeSquared(const int32 EdgeA, const int32 EdgeB, FVector& OutP1, FVector& OutP2) const
	{
		return EdgeDistToEdgeSquared(GetEdge(EdgeA), GetEdge(EdgeB), OutP1, OutP2);
	}

	FVector FCluster::GetDir(const int32 FromNode, const int32 ToNode) const
	{
		return (VtxTransforms[GetNodePointIndex(ToNode)].GetLocation() - VtxTransforms[GetNodePointIndex(FromNode)].GetLocation()).GetSafeNormal();
	}

	FVector FCluster::GetDir(const FNode& From, const FNode& To) const
	{
		return GetDir(From.Index, To.Index);
	}

	double FCluster::GetEdgeLength(const FEdge& InEdge) const
	{
		return FVector::Dist(VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
	}

	double FCluster::GetEdgeLengthSquared(const FEdge& InEdge) const
	{
		return FVector::DistSquared(VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
	}

	FVector FCluster::GetEdgeDir(const FEdge& InEdge) const
	{
		return (VtxTransforms[InEdge.Start].GetLocation() - VtxTransforms[InEdge.End].GetLocation()).GetSafeNormal();
	}

	FVector FCluster::GetEdgeDir(const int32 InEdgeIndex) const
	{
		const FEdge* Edge = EdgesDataPtr + InEdgeIndex;
		return (VtxTransforms[Edge->Start].GetLocation() - VtxTransforms[Edge->End].GetLocation()).GetSafeNormal();
	}

	FVector FCluster::GetEdgeDir(const FLink Lk) const
	{
		const FEdge* Edge = EdgesDataPtr + Lk.Edge;
		return (VtxTransforms[Edge->Start].GetLocation() - VtxTransforms[Edge->End].GetLocation()).GetSafeNormal();
	}

	FVector FCluster::GetEdgeDir(const int32 InEdgeIndex, const int32 InStartPtIndex) const
	{
		const FEdge* Edge = EdgesDataPtr + InEdgeIndex;
		return (VtxTransforms[InStartPtIndex].GetLocation() - VtxTransforms[Edge->Other(InStartPtIndex)].GetLocation()).GetSafeNormal();
	}

	FVector FCluster::GetEdgeDir(const FLink Lk, const int32 InStartPtIndex) const
	{
		const FEdge* Edge = EdgesDataPtr + Lk.Edge;
		return (VtxTransforms[InStartPtIndex].GetLocation() - VtxTransforms[Edge->Other(InStartPtIndex)].GetLocation()).GetSafeNormal();
	}

	TSharedPtr<PCGExOctree::FItemOctree> FCluster::GetNodeOctree()
	{
		if (!NodeOctree)
		{
			RebuildNodeOctree();
		}
		return NodeOctree;
	}

	TSharedPtr<PCGExOctree::FItemOctree> FCluster::GetEdgeOctree()
	{
		if (!EdgeOctree)
		{
			RebuildEdgeOctree();
		}
		return EdgeOctree;
	}

	void FCluster::RebuildNodeOctree()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FCluster::RebuildNodeOctree);

		NodeOctree = MakeShared<PCGExOctree::FItemOctree>(Bounds.GetCenter(), (Bounds.GetExtent() + FVector(10)).Length());
		for (int i = 0; i < Nodes->Num(); i++)
		{
			const FNode* Node = NodesDataPtr + i;
			const PCGExData::FConstPoint Pt = PCGExData::FConstPoint(VtxPoints, Node->PointIndex);
			NodeOctree->AddElement(PCGExOctree::FItem(Node->Index, FBoxSphereBounds(Pt.GetLocalBounds().TransformBy(Pt.GetTransform()))));
		}
	}

	void FCluster::RebuildEdgeOctree()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FCluster::RebuildEdgeOctree);

		check(Bounds.GetExtent().Length() != 0)

		EdgeOctree = MakeShared<PCGExOctree::FItemOctree>(Bounds.GetCenter(), (Bounds.GetExtent() + FVector(10)).Length());

		const int32 NumEdges = Edges->Num();

		if (!BoundedEdges)
		{
			BoundedEdges = MakeShared<TArray<FBoundedEdge>>();
			PCGExArrayHelpers::InitArray(BoundedEdges, NumEdges);

			TArray<FBoundedEdge>& BoundedEdgesRef = (*BoundedEdges);

			for (int i = 0; i < NumEdges; i++)
			{
				FBoundedEdge& NewBoundedEdge = BoundedEdgesRef[i];
				NewBoundedEdge = FBoundedEdge(this, i);
				EdgeOctree->AddElement(PCGExOctree::FItem(i, NewBoundedEdge.Bounds));
			}
		}
		else
		{
			const FBoundedEdge* BoundedEdgesDataPtr = BoundedEdges->GetData();
			for (int i = 0; i < NumEdges; i++)
			{
				EdgeOctree->AddElement(PCGExOctree::FItem(i, (BoundedEdgesDataPtr + i)->Bounds));
			}
		}
	}

	void FCluster::RebuildOctree(const EPCGExClusterClosestSearchMode Mode, const bool bForceRebuild)
	{
		switch (Mode)
		{
		case EPCGExClusterClosestSearchMode::Vtx:
			if (NodeOctree && !bForceRebuild)
			{
				return;
			}
			RebuildNodeOctree();
			break;
		case EPCGExClusterClosestSearchMode::Edge:
			if (EdgeOctree && !bForceRebuild)
			{
				return;
			}
			RebuildEdgeOctree();
			break;
		default: ;
		}
	}

	void FCluster::GatherNodesPointIndices(TArray<int32>& OutValidNodesPointIndices, const bool bValidity) const
	{
		const TArray<FNode>& NodesRef = *Nodes.Get();

		OutValidNodesPointIndices.Reset();
		OutValidNodesPointIndices.Reserve(NodesRef.Num());
		const int8 Mask = bValidity ? 1 : 0;

		for (const FNode& Node : NodesRef)
		{
			if (Node.bValid == Mask)
			{
				OutValidNodesPointIndices.Add(Node.PointIndex);
			}
		}
	}

	int32 FCluster::FindClosestNode(const FVector& Position, EPCGExClusterClosestSearchMode Mode, const int32 MinNeighbors) const
	{
		switch (Mode)
		{
		default: ;
		case EPCGExClusterClosestSearchMode::Vtx:
			return FindClosestNode(Position, MinNeighbors);
		case EPCGExClusterClosestSearchMode::Edge:
			return FindClosestNodeFromEdge(Position, MinNeighbors);
		}
	}

	int32 FCluster::FindClosestNode(const FVector& Position, const int32 MinNeighbors) const
	{
		double MaxDistance = TNumericLimits<double>::Max();
		int32 ClosestIndex = -1;

		const TArray<FNode>& NodesRef = *Nodes;

		if (NodeOctree)
		{
			auto ProcessCandidate = [&](const PCGExOctree::FItem& Item)
			{
				const FNode& Node = NodesRef[Item.Index];
				if (MinNeighbors > 0 && Node.Num() < MinNeighbors)
				{
					return;
				}
				const double Dist = FVector::DistSquared(Position, GetPos(Node));
				if (Dist < MaxDistance)
				{
					MaxDistance = Dist;
					ClosestIndex = Node.Index;
				}
			};

			NodeOctree->FindNearbyElements(Position, ProcessCandidate);
		}
		else
		{
			for (const FNode& Node : (*Nodes))
			{
				if (MinNeighbors > 0 && Node.Num() < MinNeighbors)
				{
					continue;
				}
				const double Dist = FVector::DistSquared(Position, GetPos(Node));
				if (Dist < MaxDistance)
				{
					MaxDistance = Dist;
					ClosestIndex = Node.Index;
				}
			}
		}

		return ClosestIndex;
	}

	int32 FCluster::FindClosestNodeFromEdge(const FVector& Position, const int32 MinNeighbors) const
	{
		double MaxDistance = TNumericLimits<double>::Max();
		int32 ClosestIndex = -1;

		if (EdgeOctree)
		{
			auto ProcessCandidate = [&](const PCGExOctree::FItem& Item)
			{
				const double Dist = GetPointDistToEdgeSquared(Item.Index, Position);
				if (Dist < MaxDistance)
				{
					if (MinNeighbors > 0 && !EdgeHasMinNeighbors(Item.Index, MinNeighbors))
					{
						return;
					}
					MaxDistance = Dist;
					ClosestIndex = Item.Index;
				}
			};

			EdgeOctree->FindNearbyElements(Position, ProcessCandidate);
		}
		else if (BoundedEdges)
		{
			for (const FBoundedEdge& Edge : (*BoundedEdges))
			{
				const double Dist = GetPointDistToEdgeSquared(Edge.Index, Position);
				if (Dist < MaxDistance)
				{
					if (MinNeighbors > 0 && !EdgeHasMinNeighbors(Edge.Index, MinNeighbors))
					{
						continue;
					}
					MaxDistance = Dist;
					ClosestIndex = Edge.Index;
				}
			}
		}
		else
		{
			for (const FEdge& Edge : (*Edges))
			{
				const double Dist = GetPointDistToEdgeSquared(Edge, Position);
				if (Dist < MaxDistance)
				{
					if (MinNeighbors > 0 && !EdgeHasMinNeighbors(Edge.Index, MinNeighbors))
					{
						continue;
					}
					MaxDistance = Dist;
					ClosestIndex = Edge.Index;
				}
			}
		}

		if (ClosestIndex == -1)
		{
			return -1;
		}

		const FEdge* Edge = GetEdge(ClosestIndex);
		const FNode* Start = GetEdgeStart(Edge);
		const FNode* End = GetEdgeEnd(Edge);

		ClosestIndex = FVector::DistSquared(Position, GetPos(Start)) < FVector::DistSquared(Position, GetPos(End)) ? Start->Index : End->Index;

		return ClosestIndex;
	}

	int32 FCluster::FindClosestEdge(const int32 InNodeIndex, const FVector& InPosition, const int32 MinNeighbors) const
	{
		// Find the edge incident to InNodeIndex whose segment is closest to InPosition.
		// On distance ties, prefer the edge whose direction most closely aligns with
		// the vector FROM InPosition TO the node (smallest dot product = most facing).
		if (!Nodes->IsValidIndex(InNodeIndex) || (NodesDataPtr + InNodeIndex)->IsEmpty())
		{
			return -1;
		}
		const FNode& Node = *(NodesDataPtr + InNodeIndex);

		double BestDist = TNumericLimits<double>::Max();
		double BestDot = TNumericLimits<double>::Max();

		int32 BestIndex = -1;

		const FVector Position = GetPos(Node);
		const FVector SearchDirection = (GetPos(Node) - InPosition).GetSafeNormal();

		for (const FLink Lk : Node.Links)
		{
			if (MinNeighbors > 0 && GetNode(Lk)->Links.Num() < MinNeighbors)
			{
				continue;
			}

			FVector NPos = GetPos(Lk.Node);
			const double Dist = FMath::PointDistToSegmentSquared(InPosition, Position, NPos);
			if (Dist <= BestDist)
			{
				const double Dot = FVector::DotProduct(SearchDirection, (NPos - Position).GetSafeNormal());
				if (Dist == BestDist && Dot > BestDot)
				{
					continue;
				}
				BestDot = Dot;
				BestDist = Dist;
				BestIndex = Lk.Edge;
			}
		}

		return BestIndex;
	}

	int32 FCluster::FindClosestNeighbor(const int32 NodeIndex, const FVector& Position, const int32 MinNeighborCount) const
	{
		const TArray<FNode>& NodesRef = *Nodes;
		const FNode& Node = NodesRef[NodeIndex];
		int32 Result = -1;
		double LastDist = TNumericLimits<double>::Max();
		const FVector NodePosition = GetPos(NodeIndex);

		if (NodeOctree)
		{
			auto ProcessCandidate = [&](const PCGExOctree::FItem& Item)
			{
				if (NodesRef[Item.Index].Num() < MinNeighborCount)
				{
					return;
				}
				if (const double Dist = FMath::PointDistToSegmentSquared(Position, NodePosition, GetPos(Item.Index));
					Dist < LastDist)
				{
					LastDist = Dist;
					Result = Item.Index;
				}
			};

			NodeOctree->FindNearbyElements(Position, ProcessCandidate);
		}
		else
		{
			for (const FLink Lk : Node.Links)
			{
				if (NodesRef[Lk.Node].Num() < MinNeighborCount)
				{
					continue;
				}
				if (const double Dist = FMath::PointDistToSegmentSquared(Position, NodePosition, GetPos(Lk));
					Dist < LastDist)
				{
					LastDist = Dist;
					Result = Lk.Node;
				}
			}
		}

		return Result;
	}

	int32 FCluster::FindClosestNeighbor(const int32 NodeIndex, const FVector& Position, const TSet<int32>& Exclusion, const int32 MinNeighborCount) const
	{
		const TArray<FNode>& NodesRef = *Nodes;
		const FNode& Node = NodesRef[NodeIndex];
		int32 Result = -1;
		double LastDist = TNumericLimits<double>::Max();
		const FVector NodePosition = GetPos(NodeIndex);

		if (NodeOctree)
		{
			auto ProcessCandidate = [&](const PCGExOctree::FItem& Item)
			{
				if (NodesRef[Item.Index].Num() < MinNeighborCount)
				{
					return;
				}
				if (Exclusion.Contains(Item.Index))
				{
					return;
				}
				if (const double Dist = FMath::PointDistToSegmentSquared(Position, NodePosition, GetPos(Item.Index));
					Dist < LastDist)
				{
					LastDist = Dist;
					Result = Item.Index;
				}
			};

			NodeOctree->FindNearbyElements(Position, ProcessCandidate);
		}
		else
		{
			for (const FLink Lk : Node.Links)
			{
				if (NodesRef[Lk.Node].Num() < MinNeighborCount)
				{
					continue;
				}
				if (Exclusion.Contains(Lk.Node))
				{
					continue;
				}
				if (const double Dist = FMath::PointDistToSegmentSquared(Position, NodePosition, GetPos(Lk));
					Dist < LastDist)
				{
					LastDist = Dist;
					Result = Lk.Node;
				}
			}
		}

		return Result;
	}

	void FCluster::ComputeEdgeLengths()
	{
		if (EdgeLengths)
		{
			return;
		}

		EdgeLengths = MakeShared<TArray<double>>();
		TArray<double>& LengthsRef = *EdgeLengths;

		const int32 NumEdges = Edges->Num();
		double Min = TNumericLimits<double>::Max();
		double Max = 0;
		EdgeLengths->SetNumUninitialized(NumEdges);

		for (int i = 0; i < NumEdges; i++)
		{
			const FEdge* Edge = GetEdge(i);
			const double Dist = GetDist(Edge);
			LengthsRef[i] = Dist;
			Min = FMath::Min(Dist, Min);
			Max = FMath::Max(Dist, Max);
		}

		MinEdgeLength = NumEdges > 0 ? Min : 0;
		MaxEdgeLength = Max;
	}

	void FCluster::GetConnectedNodes(const int32 FromIndex, TArray<int32>& OutIndices, const int32 SearchDepth) const
	{
		TBitArray<> Visited(false, Nodes->Num());
		Visited[FromIndex] = true; // Mark starting node as visited
		GetConnectedNodesInternal(FromIndex, OutIndices, Visited, SearchDepth);
	}

	void FCluster::GetConnectedNodes(const int32 FromIndex, TArray<int32>& OutIndices, const int32 SearchDepth, const TSet<int32>& Skip) const
	{
		TBitArray<> Visited(false, Nodes->Num());
		Visited[FromIndex] = true; // Mark starting node as visited
		GetConnectedNodesInternal(FromIndex, OutIndices, Visited, SearchDepth, Skip);
	}

	void FCluster::GetConnectedNodesInternal(const int32 FromIndex, TArray<int32>& OutIndices, TBitArray<>& Visited, const int32 SearchDepth) const
	{
		const int32 NextDepth = SearchDepth - 1;
		const FNode& RootNode = (*Nodes)[FromIndex];

		for (const FLink Lk : RootNode.Links)
		{
			if (Visited[Lk.Node])
			{
				continue;
			}

			Visited[Lk.Node] = true;
			OutIndices.Add(Lk.Node);
			if (NextDepth > 0)
			{
				GetConnectedNodesInternal(Lk.Node, OutIndices, Visited, NextDepth);
			}
		}
	}

	void FCluster::GetConnectedNodesInternal(const int32 FromIndex, TArray<int32>& OutIndices, TBitArray<>& Visited, const int32 SearchDepth, const TSet<int32>& Skip) const
	{
		const int32 NextDepth = SearchDepth - 1;
		const FNode& RootNode = (*Nodes)[FromIndex];

		for (const FLink Lk : RootNode.Links)
		{
			if (Skip.Contains(Lk.Node) || Visited[Lk.Node])
			{
				continue;
			}

			Visited[Lk.Node] = true;
			OutIndices.Add(Lk.Node);
			if (NextDepth > 0)
			{
				GetConnectedNodesInternal(Lk.Node, OutIndices, Visited, NextDepth, Skip);
			}
		}
	}

	void FCluster::GetConnectedEdges(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, const int32 SearchDepth) const
	{
		TBitArray<> VisitedNodes(false, Nodes->Num());
		TBitArray<> VisitedEdges(false, Edges->Num());
		VisitedNodes[FromNodeIndex] = true; // Mark starting node as visited
		GetConnectedEdgesInternal(FromNodeIndex, OutNodeIndices, OutEdgeIndices, VisitedNodes, VisitedEdges, SearchDepth);
	}

	void FCluster::GetConnectedEdges(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, const int32 SearchDepth, const TSet<int32>& SkipNodes, const TSet<int32>& SkipEdges) const
	{
		TBitArray<> VisitedNodes(false, Nodes->Num());
		TBitArray<> VisitedEdges(false, Edges->Num());
		VisitedNodes[FromNodeIndex] = true; // Mark starting node as visited
		GetConnectedEdgesInternal(FromNodeIndex, OutNodeIndices, OutEdgeIndices, VisitedNodes, VisitedEdges, SearchDepth, SkipNodes, SkipEdges);
	}

	void FCluster::GetConnectedEdgesInternal(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, TBitArray<>& VisitedNodes, TBitArray<>& VisitedEdges, const int32 SearchDepth) const
	{
		const int32 NextDepth = SearchDepth - 1;
		const FNode& RootNode = (*Nodes)[FromNodeIndex];

		for (const FLink Lk : RootNode.Links)
		{
			if (VisitedNodes[Lk.Node])
			{
				continue;
			}
			if (VisitedEdges[Lk.Edge])
			{
				continue;
			}

			VisitedNodes[Lk.Node] = true;
			VisitedEdges[Lk.Edge] = true;
			OutNodeIndices.Add(Lk.Node);
			OutEdgeIndices.Add(Lk.Edge);

			if (NextDepth > 0)
			{
				GetConnectedEdgesInternal(Lk.Node, OutNodeIndices, OutEdgeIndices, VisitedNodes, VisitedEdges, NextDepth);
			}
		}
	}

	void FCluster::GetConnectedEdgesInternal(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, TBitArray<>& VisitedNodes, TBitArray<>& VisitedEdges, const int32 SearchDepth, const TSet<int32>& SkipNodes, const TSet<int32>& SkipEdges) const
	{
		const int32 NextDepth = SearchDepth - 1;
		const FNode& RootNode = (*Nodes)[FromNodeIndex];

		for (const FLink Lk : RootNode.Links)
		{
			if (SkipNodes.Contains(Lk.Node) || VisitedNodes[Lk.Node])
			{
				continue;
			}
			if (SkipEdges.Contains(Lk.Edge) || VisitedEdges[Lk.Edge])
			{
				continue;
			}

			VisitedNodes[Lk.Node] = true;
			VisitedEdges[Lk.Edge] = true;
			OutNodeIndices.Add(Lk.Node);
			OutEdgeIndices.Add(Lk.Edge);

			if (NextDepth > 0)
			{
				GetConnectedEdgesInternal(Lk.Node, OutNodeIndices, OutEdgeIndices, VisitedNodes, VisitedEdges, NextDepth, SkipNodes, SkipEdges);
			}
		}
	}

	FVector FCluster::GetClosestPointOnEdge(const int32 FromIndex, const int32 ToIndex, const FVector& Position) const
	{
		return FMath::ClosestPointOnSegment(Position, GetPos(FromIndex), GetPos(ToIndex));
	}

	FVector FCluster::GetClosestPointOnEdge(const FEdge& InEdge, const FVector& Position) const
	{
		return FMath::ClosestPointOnSegment(Position, VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
	}

	FVector FCluster::GetClosestPointOnEdge(const int32 EdgeIndex, const FVector& Position) const
	{
		return GetClosestPointOnEdge(*(EdgesDataPtr + EdgeIndex), Position);
	}

	double FCluster::GetPointDistToEdgeSquared(const FEdge& InEdge, const FVector& Position) const
	{
		return FMath::PointDistToSegmentSquared(Position, VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
	}

	double FCluster::GetPointDistToEdgeSquared(const int32 EdgeIndex, const FVector& Position) const
	{
		return GetPointDistToEdgeSquared(*(EdgesDataPtr + EdgeIndex), Position);
	}

	FVector FCluster::GetCentroid(const int32 NodeIndex) const
	{
		const FNode* Node = (NodesDataPtr + NodeIndex);
		FVector Centroid = FVector::ZeroVector;
		for (const FLink Lk : Node->Links)
		{
			Centroid += GetPos(Lk.Node);
		}
		return Centroid / static_cast<double>(Node->Num());
	}

	void FCluster::GetValidEdges(TArray<FEdge>& OutValidEdges) const
	{
		const TSharedPtr<PCGExData::FPointIO> PinnedEdgesIO = EdgesIO.Pin();
		OutValidEdges.Reserve(Edges->Num());

		const int32 IOIndex = PinnedEdgesIO ? PinnedEdgesIO->IOIndex : -1;

		for (const FEdge& Edge : (*Edges))
		{
			if (!IsEdgeFullyValid(Edge))
			{
				continue;
			}
			OutValidEdges.Add_GetRef(Edge).IOIndex = IOIndex;
		}

		OutValidEdges.Shrink();
	}

	int32 FCluster::FindClosestNeighborInDirection(const int32 NodeIndex, const FVector& Direction, const int32 MinNeighborCount) const
	{
		const TArray<FNode>& NodesRef = *Nodes;

		const FNode& Node = NodesRef[NodeIndex];
		int32 Result = -1;
		double LastDot = -1;

		for (const FLink Lk : Node.Links)
		{
			if (NodesRef[Lk.Node].Num() < MinNeighborCount)
			{
				continue;
			}
			if (const double Dot = FVector::DotProduct(Direction, GetDir(NodeIndex, Lk.Node));
				Dot > LastDot)
			{
				LastDot = Dot;
				Result = Lk.Node;
			}
		}

		return Result;
	}

	TSharedPtr<TArray<FBoundedEdge>> FCluster::GetBoundedEdges(const bool bBuild)
	{
		{
			FReadScopeLock ReadScopeLock(ClusterLock);
			if (BoundedEdges)
			{
				return BoundedEdges;
			}
		}
		{
			FWriteScopeLock WriteScopeLock(ClusterLock);

			BoundedEdges = MakeShared<TArray<FBoundedEdge>>();
			PCGExArrayHelpers::InitArray(BoundedEdges, Edges->Num());

			TArray<FBoundedEdge>& ExpandedEdgesRef = (*BoundedEdges);
			if (bBuild)
			{
				for (int i = 0; i < BoundedEdges->Num(); i++)
				{
					ExpandedEdgesRef[i] = FBoundedEdge(this, i);
				}
			} // Ooof
		}

		return BoundedEdges;
	}

	void FCluster::ExpandEdges(PCGExMT::FTaskManager* TaskManager)
	{
		if (BoundedEdges)
		{
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ExpandEdgesTask);

		BoundedEdges = MakeShared<TArray<FBoundedEdge>>();
		PCGExArrayHelpers::InitArray(BoundedEdges, Edges->Num());

		ExpandEdgesTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			TArray<FBoundedEdge>& ExpandedEdgesRef = (*This->BoundedEdges);
			const FCluster* Cluster = This.Get();
			PCGEX_SCOPE_LOOP(i)
			{
				ExpandedEdgesRef[i] = FBoundedEdge(Cluster, i);
			}
		};

		ExpandEdgesTask->StartSubLoops(Edges->Num(), 256);
	}

	int32 FCluster::GetOrCreateNode_Unsafe(const int32 PointIndex)
	{
		int32 NodeIndex = NodeIndexLookup->Get(PointIndex);

		if (NodeIndex != -1)
		{
			// A non -1 entry must be a node this build created; anything else is a
			// stale/foreign lookup, which the build contract forbids.
			checkSlow(Nodes->IsValidIndex(NodeIndex) && (*Nodes)[NodeIndex].PointIndex == PointIndex)
			return NodeIndex;
		}

		NodeIndex = Nodes->Add(FNode(Nodes->Num(), PointIndex));
		NodeIndexLookup->GetMutable(PointIndex) = NodeIndex;
		Bounds += VtxTransforms[PointIndex].GetLocation();

		return NodeIndex;
	}

	void FCluster::SetCachedData(FName Key, const TSharedPtr<ICachedClusterData>& Data)
	{
		FWriteScopeLock WriteLock(ClusterLock);
		CachedData.Add(Key, Data);
	}

	void FCluster::ClearCachedData()
	{
		FWriteScopeLock WriteLock(ClusterLock);
		CachedData.Reset();
	}
}
