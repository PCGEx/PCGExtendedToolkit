// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/Union/PCGExIntersections.h"
#include "Sorting/PCGExSortingHelpers.h"

#include "PCGExH.h"

#include "Async/ParallelFor.h"
#include "Details/PCGExIntersectionDetails.h"
#include "Data/PCGExPointIO.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExEdge.h"
#include "Data/PCGExData.h"
#include "Core/PCGExUnionData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphMetadata.h"
#include "Math/PCGExMath.h"

namespace PCGExGraphs
{
	FUnionNode::FUnionNode(const PCGExData::FConstPoint& InPoint, const FVector& InCenter, const int32 InIndex)
		: Point(InPoint), Center(InCenter), Index(InIndex), CenterAccum(InCenter)
	{
		Bounds = FBoxSphereBounds(InPoint.Data->GetLocalBounds(InPoint.Index).TransformBy(InPoint.Data->GetTransform(InPoint.Index)));
	}

	FUnionGraph::FUnionGraph(const FPCGExFuseDetails& InFuseDetails, const FBox& InBounds, const TSharedPtr<PCGExData::FPointIOCollection>& InSourceCollection)
		: SourceCollection(InSourceCollection), FuseDetails(InFuseDetails), Bounds(InBounds)
	{
		Nodes.Empty();
		Edges.Empty();

		NodesUnion = MakeShared<PCGExData::FUnionMetadata>();
		EdgesUnion = MakeShared<PCGExData::FUnionMetadata>();

		if (FuseDetails.FuseMethod == EPCGExFuseMethod::Octree)
		{
			Octree = MakeUnique<FUnionNodeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		}
	}

	bool FUnionGraph::Init(FPCGExContext* InContext)
	{
		return FuseDetails.Init(InContext, nullptr);
	}

	bool FUnionGraph::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InUniqueSourceFacade, const bool SupportScopedGet)
	{
		return FuseDetails.Init(InContext, InUniqueSourceFacade);
	}

	void FUnionGraph::Reserve(const int32 NodeReserve, const int32 EdgeReserve)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionGraph::Reserve);

		if (!Octree) { NodeBinsShards.Reserve(NodeReserve); }

		Nodes.Reserve(NodeReserve);
		NodesUnion->Entries.Reserve(NodeReserve);

		const int32 EffectiveEdgeReserve = EdgeReserve < 0 ? NodeReserve : EdgeReserve;
		EdgesMapShards.Reserve(EffectiveEdgeReserve);
		Edges.Reserve(EffectiveEdgeReserve);
		EdgesUnion->Entries.Reserve(EffectiveEdgeReserve);
	}

	int32 FUnionGraph::InsertPoint(const PCGExData::FConstPoint& Point)
	{
		const FVector Origin = Point.GetLocation();

		if (!Octree)
		{
			const uint64 GridKey = FuseDetails.GetGridKey(Origin, Point.Index);
			{
				if (const int32* NodePtr = NodeBinsShards.Find(GridKey))
				{
					const int32 NodeIndex = *NodePtr;
					NodesUnion->Append(NodeIndex, Point);
					Nodes[NodeIndex]->Accumulate(Origin);
					return NodeIndex;
				}
			}

			{
				FWriteScopeLock WriteLock(UnionLock);

				// Make sure there hasn't been an insert while locking
				if (const int32* NodePtr = NodeBinsShards.Find(GridKey))
				{
					const int32 NodeIndex = *NodePtr;
					NodesUnion->Append(NodeIndex, Point);
					Nodes[NodeIndex]->Accumulate(Origin);
					return NodeIndex;
				}

				NodesUnion->NewEntry_Unsafe(Point);
				return NodeBinsShards.Add(GridKey, Nodes.Add(MakeShared<FUnionNode>(Point, Origin, Nodes.Num())));
			}
		}

		{
			FWriteScopeLock WriteScopeLock(UnionLock);
			PCGExMath::FClosestPosition ClosestNode(Origin);

			Octree->FindElementsWithBoundsTest(FuseDetails.GetOctreeBox(Origin, Point.Index), [&](const FUnionNode* ExistingNode)
			{
				const bool bIsWithin = FuseDetails.bComponentWiseTolerance ? FuseDetails.IsWithinToleranceComponentWise(Point, ExistingNode->Point) : FuseDetails.IsWithinTolerance(Point, ExistingNode->Point);

				if (bIsWithin)
				{
					ClosestNode.Update(ExistingNode->Center, ExistingNode->Index);
					return false;
				}
				return true;
			});

			if (ClosestNode.bValid)
			{
				NodesUnion->Append(ClosestNode.Index, Point);
				Nodes[ClosestNode.Index]->Accumulate(Origin);
				return ClosestNode.Index;
			}

			// Still holding the lock -- safe to insert
			const TSharedPtr<FUnionNode> Node = MakeShared<FUnionNode>(Point, Origin, Nodes.Num());
			Octree->AddElement(Node.Get());
			NodesUnion->NewEntry_Unsafe(Point);
			return Nodes.Add(Node);
		}
	}

	void FUnionGraph::InsertEdge(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, const PCGExData::FConstPoint& Edge)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IUnionData::InsertEdge);

		const int32 Start = InsertPoint(From);
		const int32 End = InsertPoint(To);

		if (Start == End) { return; } // Edge got fused entirely

		TSharedPtr<PCGExData::IUnionData> EdgeUnion;

		const uint64 H = PCGEx::H64U(Start, End);

		auto UpdateExistingUnion = [&](const int32* ExistingEdge)
		{
			EdgeUnion = EdgesUnion->Entries[*ExistingEdge];
			if (Edge.IO == -1) { EdgeUnion->Add(EdgeUnion->Num(), -1); } // Abstract tracking to get valid union data
			else { EdgeUnion->Add(Edge); }
		};

		if (const int32* ExistingEdge = EdgesMapShards.Find(H))
		{
			UpdateExistingUnion(ExistingEdge);
			return;
		}

		{
			FWriteScopeLock WriteLockEdges(EdgesLock);

			if (const int32* ExistingEdge = EdgesMapShards.Find(H))
			{
				UpdateExistingUnion(ExistingEdge);
				return;
			}

			EdgeUnion = EdgesUnion->NewEntry_Unsafe(Edge);
			EdgesMapShards.Add(H, Edges.Emplace(Edges.Num(), Start, End));
		}
	}

	void FUnionGraph::WriteNodeMetadata(const TSharedPtr<FGraph>& InGraph) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionGraph::WriteNodeMetadata)

		for (const TSharedPtr<FUnionNode>& Node : Nodes)
		{
			const TSharedPtr<PCGExData::IUnionData>& UnionData = NodesUnion->Entries[Node->Index];
			FGraphNodeMetadata& NodeMeta = InGraph->GetOrCreateNodeMetadata_Unsafe(Node->Index);
			NodeMeta.UnionSize = UnionData->Num();
		}
	}

	void FUnionGraph::WriteEdgeMetadata(const TSharedPtr<FGraph>& InGraph) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionGraph::WriteEdgeMetadata)

		const int32 NumEdges = GetNumCollapsedEdges();
		for (int i = 0; i < NumEdges; i++)
		{
			const TSharedPtr<PCGExData::IUnionData>& UnionData = EdgesUnion->Entries[i];
			FGraphEdgeMetadata& EdgeMetadata = InGraph->GetOrCreateEdgeMetadata_Unsafe(i);
			EdgeMetadata.UnionSize = UnionData->Num();
		}
	}

	void FUnionGraph::Collapse()
	{
		NumCollapsedEdges = Edges.Num();
		EdgesMapShards.Empty();
		NodeBinsShards.Empty();
		Octree.Reset();

		// Spatial sort nodes by Morton hash for deterministic ordering
		const int32 N = Nodes.Num();
		if (N <= 1) { bNodesSorted = true; return; }

		// 1. Compute Morton hash for each node center
		TArray<PCGEx::FIndexKey> MortonHash;
		MortonHash.SetNumUninitialized(N);
		for (int32 i = 0; i < N; i++)
		{
			MortonHash[i] = PCGEx::FIndexKey(i, PCGEx::MH64(Nodes[i]->GetCenter()));
		}

		// 2. Sort
		PCGExSortingHelpers::RadixSort(MortonHash);

		// 3. Build old->new remap
		TArray<int32> OldToNew;
		OldToNew.SetNumUninitialized(N);
		for (int32 i = 0; i < N; i++)
		{
			OldToNew[MortonHash[i].Index] = i;
		}

		// 4. Reorder Nodes
		TArray<TSharedPtr<FUnionNode>> SortedNodes;
		SortedNodes.SetNum(N);
		for (int32 i = 0; i < N; i++)
		{
			SortedNodes[i] = Nodes[MortonHash[i].Index];
			SortedNodes[i]->Index = i;
		}
		Nodes = MoveTemp(SortedNodes);

		// 5. Remap edges
		for (FEdge& Edge : Edges)
		{
			Edge.Start = OldToNew[Edge.Start];
			Edge.End = OldToNew[Edge.End];
		}

		// 6. Remap NodesUnion entries to match new node order
		TArray<TSharedPtr<PCGExData::IUnionData>> SortedEntries;
		SortedEntries.SetNum(N);
		for (int32 i = 0; i < N; i++)
		{
			SortedEntries[i] = NodesUnion->Entries[MortonHash[i].Index];
		}
		NodesUnion->Entries = MoveTemp(SortedEntries);

		bNodesSorted = true;
	}

#pragma region FBatchInserter

	int32 FUnionGraph::FBatchInserter::InsertPoint(const PCGExData::FConstPoint& Point)
	{
		const FVector Origin = Point.GetLocation();

		if (!Graph.Octree)
		{
			const uint64 GridKey = Graph.FuseDetails.GetGridKey(Origin, Point.Index);

			if (const int32* NodePtr = Graph.NodeBinsShards.Find(GridKey))
			{
				const int32 NodeIndex = *NodePtr;
				Graph.NodesUnion->Append_Unsafe(NodeIndex, Point);
				Graph.Nodes[NodeIndex]->Accumulate(Origin);
				return NodeIndex;
			}

			Graph.NodesUnion->NewEntry_Unsafe(Point);
			return Graph.NodeBinsShards.Add(GridKey, Graph.Nodes.Add(MakeShared<FUnionNode>(Point, Origin, Graph.Nodes.Num())));
		}

		PCGExMath::FClosestPosition ClosestNode(Origin);

		Graph.Octree->FindElementsWithBoundsTest(Graph.FuseDetails.GetOctreeBox(Origin, Point.Index), [&](const FUnionNode* ExistingNode)
		{
			const bool bIsWithin = Graph.FuseDetails.bComponentWiseTolerance ? Graph.FuseDetails.IsWithinToleranceComponentWise(Point, ExistingNode->Point) : Graph.FuseDetails.IsWithinTolerance(Point, ExistingNode->Point);

			if (bIsWithin)
			{
				ClosestNode.Update(ExistingNode->Center, ExistingNode->Index);
				return false;
			}
			return true;
		});

		if (ClosestNode.bValid)
		{
			Graph.NodesUnion->Append_Unsafe(ClosestNode.Index, Point);
			Graph.Nodes[ClosestNode.Index]->Accumulate(Origin);
			return ClosestNode.Index;
		}

		const TSharedPtr<FUnionNode> Node = MakeShared<FUnionNode>(Point, Origin, Graph.Nodes.Num());
		Graph.Octree->AddElement(Node.Get());
		Graph.NodesUnion->NewEntry_Unsafe(Point);
		return Graph.Nodes.Add(Node);
	}

	void FUnionGraph::FBatchInserter::InsertEdge(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, const PCGExData::FConstPoint& Edge)
	{
		const int32 Start = InsertPoint(From);
		const int32 End = InsertPoint(To);

		if (Start == End) { return; }

		TSharedPtr<PCGExData::IUnionData> EdgeUnion;
		const uint64 H = PCGEx::H64U(Start, End);

		if (const int32* ExistingEdge = Graph.EdgesMapShards.Find(H))
		{
			EdgeUnion = Graph.EdgesUnion->Entries[*ExistingEdge];
			if (Edge.IO == -1) { EdgeUnion->Add_Unsafe(EdgeUnion->Num(), -1); }
			else { EdgeUnion->Add_Unsafe(Edge); }
			return;
		}

		EdgeUnion = Graph.EdgesUnion->NewEntry_Unsafe(Edge);
		Graph.EdgesMapShards.Add(H, Graph.Edges.Emplace(Graph.Edges.Num(), Start, End));
	}

#pragma endregion

	FIntersectionCache::FIntersectionCache(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO)
		: PointIO(InPointIO), Graph(InGraph)
	{
		NodeTransforms = InPointIO->GetOutIn()->GetConstTransformValueRange();
	}

	bool FIntersectionCache::InitProxy(const TSharedPtr<FEdgeProxy>& Edge, const int32 Index) const
	{
		if (Index == -1) { return false; }
		if (!ValidEdges[Index]) { return false; }

		const FEdge& E = Graph->Edges[Index];
		Edge->Init(E, Positions[E.Start], Positions[E.End], Tolerance);

		return true;
	}

	void FIntersectionCache::BuildCache()
	{
		const int32 NumEdges = Graph->Edges.Num();

		ValidEdges.Init(false, NumEdges);
		LengthSquared.SetNum(NumEdges);
		Directions.SetNum(NumEdges);
		Positions.SetNum(Graph->Nodes.Num());

		for (int i = 0; i < Positions.Num(); ++i) { Positions[i] = NodeTransforms[i].GetLocation(); }

		for (const FEdge& Edge : Graph->Edges)
		{
			const FVector A = Positions[Edge.Start];
			const FVector B = Positions[Edge.End];

			const double Len = FVector::DistSquared(A, B);
			if (!Edge.bValid || FMath::IsNearlyZero(Len)) { continue; }

			const int32 Index = Edge.Index;
			ValidEdges[Index] = true;
			LengthSquared[Index] = Len;
			Directions[Index] = (A - B).GetSafeNormal();

			if (Octree) { Octree->AddElement(PCGExOctree::FItem(Index, PCGEX_BOX_TOLERANCE_INLINE(A, B, Tolerance))); }
		}
	}

	void FEdgeProxy::Init(const FEdge& InEdge, const FVector& InStart, const FVector& InEnd, const double Tolerance)
	{
		Index = InEdge.Index;
		Start = InEdge.Start;
		End = InEdge.End;
		Box = PCGEX_BOX_TOLERANCE_INLINE(InStart, InEnd, Tolerance);
	}

	bool FPointEdgeProxy::FindSplit(const int32 PointIndex, const TSharedPtr<FIntersectionCache>& Cache, FPESplit& OutSplit) const
	{
		const FVector& A = Cache->Positions[Start];
		const FVector& B = Cache->Positions[End];
		const FVector& C = Cache->Positions[PointIndex];
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(C, A, B);

		if ((ClosestPoint - A).IsNearlyZero() || (ClosestPoint - B).IsNearlyZero()) { return false; } // Overlap endpoint
		if (FVector::DistSquared(ClosestPoint, C) >= Cache->ToleranceSquared) { return false; }       // Too far

		OutSplit.ClosestPoint = ClosestPoint;
		OutSplit.Time = (FVector::DistSquared(A, ClosestPoint) / Cache->LengthSquared[Index]);
		return true;
	}

	void FPointEdgeProxy::Add(const FPESplit& Split)
	{
		CollinearPoints.Add(Split);
	}

	FPointEdgeIntersections::FPointEdgeIntersections(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const FPCGExPointEdgeIntersectionDetails* InDetails)
		: FIntersectionCache(InGraph, InPointIO), Details(InDetails)
	{
	}

	void FPointEdgeIntersections::Init(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedEdges = MakeShared<PCGExMT::TScopedArray<TSharedPtr<FPointEdgeProxy>>>(Loops);
		Tolerance = Details->FuseDetails.Tolerance;
		ToleranceSquared = FMath::Square(Tolerance);
		BuildCache();
	}

	void FPointEdgeIntersections::InsertEdges()
	{
		// Collapse scoped edges into Edges if initialized
		if (ScopedEdges) { ScopedEdges->Collapse(Edges); }

		FEdge NewEdge = FEdge{};

		UPCGBasePointData* OutPointData = PointIO->GetOut();
		TPCGValueRange<FTransform> Transforms = OutPointData->GetTransformValueRange(false);

		// Find how many new metadata needs to be reserved
		int32 EdgeReserve = 0;
		for (const TSharedPtr<FPointEdgeProxy>& PointEdgeProxy : Edges) { EdgeReserve += PointEdgeProxy->CollinearPoints.Num() + 1; }

		Graph->ReserveForEdges(EdgeReserve);

		for (const TSharedPtr<FPointEdgeProxy>& EdgeProxy : Edges)
		{
			const int32 IOIndex = Graph->Edges[EdgeProxy->Index].IOIndex;
			const int32 RootIndex = Graph->FindEdgeMetadataRootIndex_Unsafe(EdgeProxy->Index);

			int32 A = EdgeProxy->Start;
			for (const FPESplit Split : EdgeProxy->CollinearPoints)
			{
				const int32 B = Split.Index;

				//TODO: IOIndex required
				if (Graph->InsertEdge_Unsafe(A, B, NewEdge, IOIndex))
				{
					Graph->GetOrCreateNodeMetadata_Unsafe(B).Type = EPCGExIntersectionType::PointEdge;
					FGraphEdgeMetadata& NewEdgeMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
					NewEdgeMeta.Type = EPCGExIntersectionType::PointEdge;
					NewEdgeMeta.bIsSubEdge = true;
					if (Details->bSnapOnEdge) { Transforms[Graph->Nodes[Split.Index].PointIndex].SetLocation(Split.ClosestPoint); }
				}
				else if (FGraphEdgeMetadata* ExistingEdgeMeta = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
				{
					ExistingEdgeMeta->UnionSize++;
					ExistingEdgeMeta->bIsSubEdge = true;
				}

				A = B;
			}

			// Insert last edge
			if (Graph->InsertEdge_Unsafe(A, EdgeProxy->End, NewEdge, IOIndex))
			{
				FGraphEdgeMetadata& NewEdgeMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
				NewEdgeMeta.Type = EPCGExIntersectionType::PointEdge;
				NewEdgeMeta.bIsSubEdge = true;
			}
			else if (FGraphEdgeMetadata* ExistingMetadataPtr = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
			{
				ExistingMetadataPtr->UnionSize++;
				ExistingMetadataPtr->bIsSubEdge = true;
			}
		}
	}

	void FPointEdgeIntersections::BlendIntersection(const int32 Index, PCGExBlending::FMetadataBlender* Blender) const
	{
		const TSharedPtr<FPointEdgeProxy>& PointEdgeProxy = Edges[Index];

		if (PointEdgeProxy->CollinearPoints.IsEmpty()) { return; }

		const FEdge& SplitEdge = Graph->Edges[PointEdgeProxy->Index];

		const int32 A = SplitEdge.Start;
		const int32 B = SplitEdge.End;

		const TPCGValueRange<FTransform> Transforms = PointIO->GetOut()->GetTransformValueRange(false);

		for (const FPESplit Split : PointEdgeProxy->CollinearPoints)
		{
			const int32 TargetIndex = Graph->Nodes[Split.Index].PointIndex;
			const FVector& PreBlendLocation = Transforms[TargetIndex].GetLocation();

			Blender->Blend(A, B, TargetIndex, 0.5); // TODO : Compute proper lerp

			Transforms[TargetIndex].SetLocation(PreBlendLocation);
		}
	}

	void FindCollinearNodes(const TSharedPtr<FPointEdgeIntersections>& InIntersections, const TSharedPtr<FPointEdgeProxy>& EdgeProxy, const bool bEnableSelfIntersection)
	{
		const TConstPCGValueRange<FTransform> Transforms = InIntersections->NodeTransforms;
		const TSharedPtr<FGraph> Graph = InIntersections->Graph;

		const FEdge& IEdge = Graph->Edges[EdgeProxy->Index];
		FPESplit Split = FPESplit{};

		// Pre-compute the set of IO indices this edge's root belongs to.
		// Used to reject nodes from the same source cluster (self-intersection filter).
		TSet<int32> RootIOIndices;
		if (!bEnableSelfIntersection)
		{
			const int32 EdgeRootIndex = InIntersections->Graph->FindEdgeMetadataRootIndex_Unsafe(EdgeProxy->Index);
			RootIOIndices = Graph->EdgesUnion->Entries[EdgeRootIndex]->GetIOSet();
		}

		InIntersections->PointIO->GetOutIn()->GetPointOctree().FindElementsWithBoundsTest(EdgeProxy->Box, [&](const PCGPointOctree::FPointRef& PointRef)
		{
			const int32 PointIndex = PointRef.Index;

			if (!Transforms.IsValidIndex(PointIndex)) { return; }
			const FNode& Node = InIntersections->Graph->Nodes[PointIndex];

			if (!Node.bValid) { return; }

			const FVector Position = Transforms[Node.PointIndex].GetLocation();

			if (!EdgeProxy->Box.IsInside(Position)) { return; } // Refine octree broad-phase
			if (IEdge.Contains(Node.PointIndex)) { return; }    // Skip own endpoints
			if (!EdgeProxy->FindSplit(Node.PointIndex, InIntersections, Split)) { return; } // Not within tolerance

			// Reject nodes that belong to the same source IO as this edge (self-intersection filter)
			if (!bEnableSelfIntersection && Graph->NodesUnion->IOIndexOverlap(Node.Index, RootIOIndices)) { return; }

			Split.Index = Node.Index;
			EdgeProxy->Add(Split);
		});
	}

	bool FEdgeEdgeProxy::FindSplit(const FEdge& OtherEdge, const TSharedPtr<FIntersectionCache>& Cache)
	{
		const FVector A0 = Cache->Positions[Start];
		const FVector B0 = Cache->Positions[End];
		const FVector A1 = Cache->Positions[OtherEdge.Start];
		const FVector B1 = Cache->Positions[OtherEdge.End];

		FVector A;
		FVector B;
		FMath::SegmentDistToSegment(A0, B0, A1, B1, A, B);

		if (FVector::DistSquared(A, B) >= Cache->ToleranceSquared) { return false; }

		// We're being strict about edge/edge
		if (A == A0 || A == B0 || B == A1 || B == B1) { return false; }

		FEESplit& Split = Crossings.Emplace_GetRef().Split;

		Split.A = Index;
		Split.B = OtherEdge.Index;
		Split.Center = FMath::Lerp(A, B, 0.5);
		Split.TimeA = FVector::DistSquared(A0, A) / Cache->LengthSquared[Index];
		Split.TimeB = FVector::DistSquared(A1, B) / Cache->LengthSquared[OtherEdge.Index];

		return true;
	}

	FEdgeEdgeIntersections::FEdgeEdgeIntersections(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<FUnionGraph>& InUnionGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const FPCGExEdgeEdgeIntersectionDetails* InDetails)
		: FIntersectionCache(InGraph, InPointIO), Details(InDetails)
	{
		Tolerance = Details->Tolerance;
		ToleranceSquared = Details->ToleranceSquared;
		Octree = MakeShared<PCGExOctree::FItemOctree>(InUnionGraph->Bounds.GetCenter(), InUnionGraph->Bounds.GetExtent().Length() + (Details->Tolerance * 2));
	}

	void FEdgeEdgeIntersections::Init(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedEdges = MakeShared<PCGExMT::TScopedArray<TSharedPtr<FEdgeEdgeProxy>>>(Loops);
		BuildCache();
	}

	void FEdgeEdgeIntersections::Collapse(const int32 InReserve)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FEdgeEdgeIntersections::Collapse);

		// Collapse and dedupe crossings nodes
		// TODO : Need to keep only one of the two existing crossings, and update the duplicate one with its final insertion index

		ScopedEdges->Collapse(Edges);

		if (Edges.IsEmpty()) { return; }

		const int32 StartIndex = Graph->Nodes.Num();
		TMap<uint64, int32> IdxMap; // Edge Index x Edge Index unsigned hash mapped to final crossing index
		IdxMap.Reserve(InReserve);

		// TODO : Crossings need to exist only once to be inserted
		// Flush existing crossing as we process them and only keep the (unordered) final node Index

		int32 Offset = 0;
		for (const TSharedPtr<FEdgeEdgeProxy>& EdgeProxy : Edges)
		{
			for (FEECrossing& Crossing : EdgeProxy->Crossings)
			{
				const uint64 Key = Crossing.Split.H64U();
				if (const int32* Existing = IdxMap.Find(Key))
				{
					Crossing.Index = *Existing;
				}
				else
				{
					Crossing.Index = IdxMap.Add(Key, StartIndex + Offset);
					UniqueCrossings.Add(Crossing);
					Offset++;
				}
			}
		}

		// Sort crossings
		ParallelFor(Edges.Num(), [&](int32 i)
		{
			const TSharedPtr<FEdgeEdgeProxy>& EdgeProxy = Edges[i];
			EdgeProxy->Crossings.Sort([GraphIndex = EdgeProxy->Index](const FEECrossing& A, const FEECrossing& B) { return A.GetTime(GraphIndex) < B.GetTime(GraphIndex); });
		});
	}

	bool FEdgeEdgeIntersections::InsertNodes(const int32 InReserve)
	{
		Collapse(InReserve);

		if (UniqueCrossings.IsEmpty()) { return false; }

		TRACE_CPUPROFILER_EVENT_SCOPE(FEdgeEdgeIntersections::InsertNodes);

		// Insert new nodes
		int32 StartIndex = -1;
		Graph->AddNodes(UniqueCrossings.Num(), StartIndex);

		UPCGBasePointData* MutablePoints = PointIO->GetOut();
		const int32 NumPoints = Graph->Nodes.Num();
		MutablePoints->SetNumPoints(NumPoints);

		UPCGMetadata* Metadata = PointIO->GetOut()->Metadata;

		TPCGValueRange<int64> MetadataEntries = MutablePoints->GetMetadataEntryValueRange(false);

		TArray<TTuple<int64, int64>> DelayedEntries;
		DelayedEntries.SetNum(NumPoints - StartIndex);

		int32 WriteIndex = 0;
		for (int i = StartIndex; i < NumPoints; i++)
		{
			MetadataEntries[i] = Metadata->AddEntryPlaceholder();
			DelayedEntries[WriteIndex++] = MakeTuple(MetadataEntries[i], PCGInvalidEntryKey);
		}

		Metadata->AddDelayedEntries(DelayedEntries);

		return true;
	}

	void FEdgeEdgeIntersections::InsertEdges()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FEdgeEdgeIntersections::InsertEdges);

		FEdge NewEdge = FEdge{};

		// Find how many new metadata needs to be reserved
		int32 EdgeReserve = 0;
		for (TSharedPtr<FEdgeEdgeProxy>& EdgeProxy : Edges) { EdgeReserve += EdgeProxy->Crossings.Num() + 1; }

		Graph->ReserveForEdges(EdgeReserve);

		for (TSharedPtr<FEdgeEdgeProxy>& EdgeProxy : Edges)
		{
			const int32 IOIndex = Graph->Edges[EdgeProxy->Index].IOIndex;
			const int32 EdgeRootIndex = Graph->FindEdgeMetadataRootIndex_Unsafe(EdgeProxy->Index);

			int32 A = EdgeProxy->Start;
			int32 B = -1;

			for (const FEECrossing& Crossing : EdgeProxy->Crossings)
			{
				B = Crossing.Index;

				//BUG: this is the wrong edge IOIndex

				if (Graph->InsertEdge_Unsafe(A, B, NewEdge, IOIndex))
				{
					Graph->GetOrCreateNodeMetadata_Unsafe(B).Type = EPCGExIntersectionType::EdgeEdge;
					FGraphEdgeMetadata& NewEdgeMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, EdgeRootIndex);
					NewEdgeMeta.Type = EPCGExIntersectionType::EdgeEdge;
					NewEdgeMeta.bIsSubEdge = true;
				}
				else if (FGraphEdgeMetadata* ExistingEdgeMeta = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
				{
					ExistingEdgeMeta->UnionSize++;
					ExistingEdgeMeta->bIsSubEdge = true;
				}

				A = B;
			}

			// Insert last edge
			if (Graph->InsertEdge_Unsafe(A, EdgeProxy->End, NewEdge, IOIndex))
			{
				FGraphEdgeMetadata& NewEdgeMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, EdgeRootIndex);
				NewEdgeMeta.Type = EPCGExIntersectionType::EdgeEdge;
				NewEdgeMeta.bIsSubEdge = true;
			}
			else if (FGraphEdgeMetadata* ExistingEdgeMeta = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
			{
				ExistingEdgeMeta->UnionSize++;
				ExistingEdgeMeta->bIsSubEdge = true;
			}
		}
	}

	void FEdgeEdgeIntersections::BlendIntersection(const int32 Index, const TSharedRef<PCGExBlending::FMetadataBlender>& Blender, TArray<PCGEx::FOpStats>& Trackers) const
	{
		const FEECrossing& Crossing = UniqueCrossings[Index];

		const int32 Target = Graph->Nodes[Crossing.Index].PointIndex;
		Blender->BeginMultiBlend(Target, Trackers);

		const int32 A1 = Graph->Nodes[Graph->Edges[Crossing.Split.A].Start].PointIndex;
		const int32 A2 = Graph->Nodes[Graph->Edges[Crossing.Split.A].End].PointIndex;
		const int32 B1 = Graph->Nodes[Graph->Edges[Crossing.Split.B].Start].PointIndex;
		const int32 B2 = Graph->Nodes[Graph->Edges[Crossing.Split.B].End].PointIndex;

		Blender->MultiBlend(A1, Target, Crossing.Split.TimeA, Trackers);
		Blender->MultiBlend(A2, Target, 1 - Crossing.Split.TimeA, Trackers);
		Blender->MultiBlend(B1, Target, Crossing.Split.TimeB, Trackers);
		Blender->MultiBlend(B2, Target, 1 - Crossing.Split.TimeB, Trackers);

		Blender->EndMultiBlend(Target, Trackers);

		PointIO->GetOutPoint(Target).SetLocation(Crossing.Split.Center);
	}

	void FindOverlappingEdges(const TSharedPtr<FEdgeEdgeIntersections>& InIntersections, const TSharedPtr<FEdgeEdgeProxy>& EdgeProxy, const bool bEnableSelfIntersection)
	{
		const int32 GraphIndex = EdgeProxy->Index;
		const int32 Start = EdgeProxy->Start;
		const int32 End = EdgeProxy->End;

		const TArray<FVector>& Directions = InIntersections->Directions;

		// Pre-compute the set of IO indices this edge's root belongs to.
		// Used to reject edges from the same source cluster (self-intersection filter).
		TSharedPtr<PCGExData::FUnionMetadata> EdgesUnion;
		TSet<int32> RootIOIndices;
		if (!bEnableSelfIntersection)
		{
			const int32 RootIndex = InIntersections->Graph->FindEdgeMetadata_Unsafe(GraphIndex)->RootIndex;
			EdgesUnion = InIntersections->Graph->EdgesUnion;
			RootIOIndices = EdgesUnion->Entries[RootIndex]->GetIOSet();
		}

		InIntersections->Octree->FindElementsWithBoundsTest(EdgeProxy->Box, [&](const PCGExOctree::FItem& Item)
		{
			const FEdge& OtherEdge = InIntersections->Graph->Edges[Item.Index];

			// Skip invalid edges, self, and edges that share endpoints
			if (!InIntersections->ValidEdges[Item.Index] || Item.Index == GraphIndex || Start == OtherEdge.Start || Start == OtherEdge.End || End == OtherEdge.End || End == OtherEdge.Start) { return; }

			// Optional angle filter
			if (InIntersections->Details->bUseMinAngle || InIntersections->Details->bUseMaxAngle)
			{
				if (!InIntersections->Details->CheckDot(FMath::Abs(FVector::DotProduct(Directions[GraphIndex], Directions[OtherEdge.Index])))) { return; }
			}

			// Self-intersection filter: reject edges from the same source IO (most expensive check, done last)
			if (!bEnableSelfIntersection && EdgesUnion->IOIndexOverlap(InIntersections->Graph->FindEdgeMetadata_Unsafe(OtherEdge.Index)->RootIndex, RootIOIndices)) { return; }

			EdgeProxy->FindSplit(OtherEdge, InIntersections);
		});
	}
}
