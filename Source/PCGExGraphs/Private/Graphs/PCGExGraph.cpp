// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExGraph.h"

#include "PCGExH.h"
#include "Clusters/PCGExEdge.h"
#include "Core/PCGExMTCommon.h"
#include "Graphs/PCGExSubGraph.h"
#include "HAL/PlatformAtomics.h"

namespace PCGExGraphs
{
	FGraph::FGraph(const int32 InNumNodes)
	{
		int32 StartNodeIndex = 0;
		AddNodes(InNumNodes, StartNodeIndex);
	}

	void FGraph::ReserveForEdges(const int32 UpcomingAdditionCount)
	{
		const int32 ExpectedEdgeTotal = Edges.Num() + UpcomingAdditionCount;
		UniqueEdges.Reserve(ExpectedEdgeTotal);
		Edges.Reserve(ExpectedEdgeTotal);

		if (ExpectedEdgeTotal > EdgeMetadata.Num())
		{
			EdgeMetadata.SetNum(ExpectedEdgeTotal);
		}
		const int32 NumNodes = Nodes.Num();
		if (NumNodes > NodeMetadata.Num())
		{
			NodeMetadata.SetNum(NumNodes);
		}
	}

	bool FGraph::InsertEdge_Unsafe(const int32 A, const int32 B, FEdge& OutEdge, const int32 IOIndex)
	{
		
#if WITH_EDITOR
		if (!ensure(A != B))
		{
			return false;
		}
#endif
		
		const uint64 Hash = PCGEx::H64U(A, B);
		if (const int32* EdgeIndex = UniqueEdges.Find(Hash))
		{
			OutEdge.Index = *EdgeIndex;
			return false;
		}

		OutEdge = Edges.Emplace_GetRef(Edges.Num(), A, B, -1, IOIndex);
		UniqueEdges.Add(Hash, (OutEdge.Index = Edges.Num() - 1));

		Nodes[A].LinkEdge(OutEdge.Index);
		Nodes[B].LinkEdge(OutEdge.Index);

		return true;
	}

	bool FGraph::InsertEdge(const int32 A, const int32 B, FEdge& OutEdge, const int32 IOIndex)
	{
		FWriteScopeLock WriteLock(GraphLock);
		return InsertEdge_Unsafe(A, B, OutEdge, IOIndex);
	}

	bool FGraph::InsertEdge_Unsafe(const FEdge& Edge)
	{
		uint64 H = Edge.H64U();
		if (UniqueEdges.Contains(H))
		{
			return false;
		}

		FEdge& NewEdge = Edges.Emplace_GetRef(Edge);
		UniqueEdges.Add(H, (NewEdge.Index = Edges.Num() - 1));

		Nodes[Edge.Start].LinkEdge(NewEdge.Index);
		Nodes[Edge.End].LinkEdge(NewEdge.Index);

		return true;
	}

	bool FGraph::InsertEdge(const FEdge& Edge)
	{
		FWriteScopeLock WriteLock(GraphLock);
		return InsertEdge_Unsafe(Edge);
	}

	bool FGraph::InsertEdge_Unsafe(const FEdge& Edge, FEdge& OutEdge, const int32 InIOIndex)
	{
		return InsertEdge_Unsafe(Edge.Start, Edge.End, OutEdge, InIOIndex);
	}

	bool FGraph::InsertEdge(const FEdge& Edge, FEdge& OutEdge, const int32 InIOIndex)
	{
		return InsertEdge(Edge.Start, Edge.End, OutEdge, InIOIndex);
	}

	void FGraph::InsertEdges(const TArray<uint64>& InEdges, const int32 InIOIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::InsertEdges)

		FWriteScopeLock WriteLock(GraphLock);
		uint32 A;
		uint32 B;

		UniqueEdges.Reserve(UniqueEdges.Num() + InEdges.Num());
		Edges.Reserve(Edges.Num() + InEdges.Num());

		for (const uint64 E : InEdges)
		{
			if (UniqueEdges.Contains(E))
			{
				continue;
			}

			PCGEx::H64(E, A, B);
			
#if WITH_EDITOR
			if (!ensure(A != B))
			{
				continue;
			}
#endif
			
			const int32 EdgeIndex = Edges.Emplace(Edges.Num(), A, B, -1, InIOIndex);

			UniqueEdges.Add(E, EdgeIndex);
			Nodes[A].LinkEdge(EdgeIndex);
			Nodes[B].LinkEdge(EdgeIndex);
		}

		UniqueEdges.Shrink();
	}

	int32 FGraph::InsertEdges(const TArray<FEdge>& InEdges)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::InsertEdges)

		FWriteScopeLock WriteLock(GraphLock);
		const int32 StartIndex = Edges.Num();

		UniqueEdges.Reserve(UniqueEdges.Num() + InEdges.Num());
		Edges.Reserve(Edges.Num() + InEdges.Num());

		for (const FEdge& E : InEdges)
		{
			InsertEdge_Unsafe(E);
		}
		return StartIndex;
	}

	void FGraph::AdoptEdges(TArray<FEdge>& InEdges, const bool bBuildAdjacency)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::AdoptEdges);

		FWriteScopeLock WriteLock(GraphLock);

		Edges = MoveTemp(InEdges);
		const int32 NumEdges = Edges.Num();

		if (!bBuildAdjacency)
		{
			// Compile-only adoption: the graph goes straight to BuildSubGraphs and
			// compilation, neither of which needs the dedup map or per-node links.
			// Metadata arrays stay empty; Find*Metadata_Unsafe bounds-check against them.
			bHasNodeLinks = false;
			return;
		}

		UniqueEdges.Reserve(NumEdges);

		for (int32 i = 0; i < NumEdges; i++)
		{
			FEdge& Edge = Edges[i];
			UniqueEdges.Add(Edge.H64U(), i);
			Nodes[Edge.Start].LinkEdge(i);
			Nodes[Edge.End].LinkEdge(i);
		}

		// Initialize edge metadata arrays
		EdgeMetadata.SetNum(NumEdges);
	}

	FEdge* FGraph::FindEdge_Unsafe(const uint64 Hash)
	{
		const int32* Index = UniqueEdges.Find(Hash);
		if (!Index)
		{
			return nullptr;
		}
		return (Edges.GetData() + *Index);
	}

	FEdge* FGraph::FindEdge_Unsafe(const int32 A, const int32 B)
	{
		return FindEdge(PCGEx::H64U(A, B));
	}

	FEdge* FGraph::FindEdge(const uint64 Hash)
	{
		FReadScopeLock ReadScopeLock(GraphLock);
		const int32* Index = UniqueEdges.Find(Hash);
		if (!Index)
		{
			return nullptr;
		}
		return (Edges.GetData() + *Index);
	}

	FEdge* FGraph::FindEdge(const int32 A, const int32 B)
	{
		return FindEdge(PCGEx::H64U(A, B));
	}

	FGraphEdgeMetadata& FGraph::GetOrCreateEdgeMetadata(const int32 EdgeIndex, const int32 RootIndex)
	{
		{
			FReadScopeLock ReadScopeLock(MetadataLock);
			if (EdgeIndex < EdgeMetadata.Num() && EdgeMetadata[EdgeIndex].EdgeIndex != -1)
			{
				return EdgeMetadata[EdgeIndex];
			}
		}
		{
			FWriteScopeLock WriteScopeLock(MetadataLock);
			check(EdgeIndex < EdgeMetadata.Num()) // All callers must pre-size via ReserveForEdges/AdoptEdges

			if (EdgeMetadata[EdgeIndex].EdgeIndex == -1)
			{
				EdgeMetadata[EdgeIndex] = FGraphEdgeMetadata(EdgeIndex, RootIndex);
				bHasAnyEdgeMetadata = true;
			}
			return EdgeMetadata[EdgeIndex];
		}
	}

	void FGraph::InsertEdges_Unsafe(const TSet<uint64>& InEdges, const int32 InIOIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::InsertEdges_Unsafe);

		uint32 A;
		uint32 B;

		UniqueEdges.Reserve(UniqueEdges.Num() + InEdges.Num());
		Edges.Reserve(UniqueEdges.Num() + InEdges.Num());

		for (const uint64& E : InEdges)
		{
			if (UniqueEdges.Contains(E))
			{
				continue;
			}

			PCGEx::H64(E, A, B);

#if WITH_EDITOR
			if (!ensure(A != B))
			{
				continue;
			}
#endif

			const int32 EdgeIndex = Edges.Emplace(Edges.Num(), A, B);
			UniqueEdges.Add(E, EdgeIndex);
			Nodes[A].LinkEdge(EdgeIndex);
			Nodes[B].LinkEdge(EdgeIndex);
			Edges[EdgeIndex].IOIndex = InIOIndex;
		}
	}

	void FGraph::InsertEdges(const TSet<uint64>& InEdges, const int32 InIOIndex)
	{
		FWriteScopeLock WriteLock(GraphLock);
		InsertEdges_Unsafe(InEdges, InIOIndex);
	}

	TArrayView<FNode> FGraph::AddNodes(const int32 NumNewNodes, int32& OutStartIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::AddNodes);

		FWriteScopeLock WriteLock(GraphLock);
		OutStartIndex = Nodes.Num();
		const int32 TotalNum = OutStartIndex + NumNewNodes;
		Nodes.Reserve(TotalNum);
		for (int i = OutStartIndex; i < TotalNum; i++)
		{
			Nodes.Emplace(i, i);
		}

		// Grow node metadata arrays to match
		if (TotalNum > NodeMetadata.Num())
		{
			NodeMetadata.SetNum(TotalNum);
		}

		return MakeArrayView(Nodes.GetData() + OutStartIndex, NumNewNodes);
	}

	void FGraph::BuildSubGraphs(const FPCGExGraphBuilderDetails& Limits, TArray<int32>& OutValidNodes)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraph::BuildSubGraphs);

		// Connected components via union-find over the flat edge list, replacing the
		// previous sequential BFS: linear scans over contiguous arrays instead of
		// pointer-chasing through per-node Links, exact component sizes known before
		// any subgraph is allocated, and components that fail size limits are culled
		// before being materialized.
		// Nodes within a component are gathered in ascending index order (the BFS
		// gathered them in traversal order); deterministic output ordering is
		// re-established downstream (Morton/radix sorts in the graph compilation).

		const int32 NumNodes = Nodes.Num();
		const int32 NumEdges = Edges.Num();

		// Edgeless nodes can never belong to a subgraph. When per-node links were
		// not built (compile-only adoption), incidence is derived from the edge list.
		TArray<int8> NodeHasEdges;
		if (!bHasNodeLinks)
		{
			NodeHasEdges.Init(0, NumNodes);
			for (const FEdge& Edge : Edges)
			{
				NodeHasEdges[static_cast<int32>(Edge.Start)] = 1;
				NodeHasEdges[static_cast<int32>(Edge.End)] = 1;
			}
		}

		// Validity is also snapshot into a compact array: the passes below hammer it
		// with random reads, and one byte per node keeps that working set
		// cache-resident instead of striding through the much larger FNode structs.
		TArray<int8> NodeValid;
		NodeValid.SetNumUninitialized(NumNodes);

		PCGExMT::ParallelOrSequential(
			NumNodes,
			[&](const int32 i)
			{
				FNode& Node = Nodes[i];
				const bool bIsolated = bHasNodeLinks ? static_cast<bool>(Node.IsEmpty()) : !NodeHasEdges[i];
				if (!Node.bValid || bIsolated)
				{
					Node.bValid = false;
				}
				NodeValid[i] = Node.bValid;
			});

		TArray<int32> Parent;
		Parent.SetNumUninitialized(NumNodes);
		int32* ParentData = Parent.GetData();
		PCGExMT::ParallelOrSequential(NumNodes, [&](const int32 i)
		{
			ParentData[i] = i;
		});

		// Iterative find with opportunistic CAS path-halving. Links always attach the
		// larger root under the smaller one, so parents only ever move toward smaller
		// indices; a stale relaxed read is just a valid ancestor and the chase
		// converges regardless of thread interleaving.
		auto FindRoot = [ParentData](int32 X) -> int32
		{
			while (true)
			{
				const int32 P = FPlatformAtomics::AtomicRead_Relaxed(ParentData + X);
				if (P == X)
				{
					return X;
				}

				const int32 GP = FPlatformAtomics::AtomicRead_Relaxed(ParentData + P);
				if (GP == P)
				{
					return P;
				}

				// Halving is opportunistic; a failed exchange means another thread
				// already moved this link further along.
				FPlatformAtomics::InterlockedCompareExchange(ParentData + X, GP, P);
				X = GP;
			}
		};

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildSubGraphs::Union);

			// Lock-free union by minimum index: every component deterministically
			// converges to its smallest node index as root, independent of thread
			// scheduling, so the final state is identical to a sequential pass.
			PCGExMT::ParallelOrSequential(
				NumEdges,
				[&](const int32 EdgeIndex)
				{
					const FEdge& Edge = Edges[EdgeIndex];
					if (!Edge.bValid)
					{
						return;
					}

					int32 U = static_cast<int32>(Edge.Start);
					int32 V = static_cast<int32>(Edge.End);

					if (!NodeValid[U] || !NodeValid[V])
					{
						return;
					}

					while (true)
					{
						U = FindRoot(U);
						V = FindRoot(V);

						if (U == V)
						{
							break;
						}

						if (U > V)
						{
							Swap(U, V);
						}

						// Attach the larger root under the smaller one; only valid if
						// V is still its own root, otherwise retry from the new roots.
						if (FPlatformAtomics::InterlockedCompareExchange(ParentData + V, U, V) == V)
						{
							break;
						}
					}
				});

			// Flatten so every node points directly at its final root; later passes
			// then resolve components with a single read.
			PCGExMT::ParallelOrSequential(
				NumNodes,
				[&](const int32 i)
				{
					FPlatformAtomics::InterlockedExchange(ParentData + i, FindRoot(i));
				});
		}

		// Assign compact component ids ordered by minimum node index -- the same
		// order in which the BFS used to discover components.
		int32 NumComponents = 0;
		int32 TotalExportedNodes = 0;

		TArray<int32> NodeComponent;
		NodeComponent.Init(-1, NumNodes);

		TArray<int32> RootToComponent;
		RootToComponent.Init(-1, NumNodes);

		TArray<int32> ComponentNodeCounts;
		TArray<int32> ComponentEdgeCounts;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildSubGraphs::Label);

			for (int32 i = 0; i < NumNodes; i++)
			{
				if (!NodeValid[i])
				{
					continue;
				}

				const int32 Root = Parent[i];
				int32& Component = RootToComponent[Root];
				if (Component == -1)
				{
					Component = NumComponents++;
					ComponentNodeCounts.Add(0);
				}
				NodeComponent[i] = Component;
				ComponentNodeCounts[Component]++;
			}

			ComponentEdgeCounts.Init(0, NumComponents);

			for (const FEdge& Edge : Edges)
			{
				if (!Edge.bValid)
				{
					continue;
				}

				// Both endpoints share the same component by construction; an edge
				// with an invalid endpoint belongs to none (mirrors the BFS, which
				// never collected such edges).
				const int32 Component = NodeComponent[static_cast<int32>(Edge.Start)];
				if (Component == -1 || NodeComponent[static_cast<int32>(Edge.End)] == -1)
				{
					continue;
				}

				ComponentEdgeCounts[Component]++;
			}
		}

		// Evaluate size limits on counts alone - nothing has been allocated yet.
		// Components that fail limits are invalidated; edgeless components are
		// silently dropped (not invalidated, not exported), as before.
		const int32 SubGraphsBase = SubGraphs.Num();
		int32 NumNewSubGraphs = 0;

		TArray<int8> ComponentCulled;
		ComponentCulled.SetNumUninitialized(NumComponents);

		TArray<int32> ComponentSubGraph;
		ComponentSubGraph.SetNumUninitialized(NumComponents);

		for (int32 c = 0; c < NumComponents; c++)
		{
			const bool bMeetsLimits = Limits.IsValid(ComponentNodeCounts[c], ComponentEdgeCounts[c]);
			ComponentCulled[c] = bMeetsLimits ? 0 : 1;

			if (bMeetsLimits && ComponentEdgeCounts[c] > 0)
			{
				ComponentSubGraph[c] = NumNewSubGraphs++;
				TotalExportedNodes += ComponentNodeCounts[c];
			}
			else
			{
				ComponentSubGraph[c] = -1;
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildSubGraphs::Cull);

			PCGExMT::ParallelOrSequential(
				NumNodes,
				[&](const int32 i)
				{
					const int32 Component = NodeComponent[i];
					if (Component != -1 && ComponentCulled[Component])
					{
						Nodes[i].bValid = false;
						NodeValid[i] = 0;
					}
				});

			PCGExMT::ParallelOrSequential(
				NumEdges,
				[&](const int32 i)
				{
					FEdge& Edge = Edges[i];
					if (!Edge.bValid)
					{
						return;
					}

					const int32 Component = NodeComponent[static_cast<int32>(Edge.Start)];
					if (Component == -1 || NodeComponent[static_cast<int32>(Edge.End)] == -1)
					{
						return;
					}

					if (ComponentCulled[Component])
					{
						Edge.bValid = false;
					}
				});
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildSubGraphs::Gather);

			SubGraphs.Reserve(SubGraphsBase + NumNewSubGraphs);
			for (int32 c = 0; c < NumComponents; c++)
			{
				if (ComponentSubGraph[c] == -1)
				{
					continue;
				}

				TSharedPtr<FSubGraph> SubGraph = MakeShared<FSubGraph>();
				SubGraph->WeakParentGraph = SharedThis(this);
				SubGraph->Nodes.Reserve(ComponentNodeCounts[c]);
				SubGraph->Edges.Reserve(ComponentEdgeCounts[c]);
				SubGraphs.Add(SubGraph.ToSharedRef());
			}

			for (int32 i = 0; i < NumNodes; i++)
			{
				const int32 Component = NodeComponent[i];
				if (Component == -1)
				{
					continue;
				}

				const int32 SubGraphIndex = ComponentSubGraph[Component];
				if (SubGraphIndex == -1)
				{
					continue;
				}

				SubGraphs[SubGraphsBase + SubGraphIndex]->Nodes.Add(i);
			}

			for (const FEdge& Edge : Edges)
			{
				if (!Edge.bValid)
				{
					continue;
				}

				const int32 Component = NodeComponent[static_cast<int32>(Edge.Start)];
				if (Component == -1 || NodeComponent[static_cast<int32>(Edge.End)] == -1)
				{
					continue;
				}

				const int32 SubGraphIndex = ComponentSubGraph[Component];
				if (SubGraphIndex == -1)
				{
					continue;
				}

				SubGraphs[SubGraphsBase + SubGraphIndex]->Add(Edge);
			}

			OutValidNodes.Reserve(OutValidNodes.Num() + TotalExportedNodes);
			for (int32 s = SubGraphsBase; s < SubGraphs.Num(); s++)
			{
				OutValidNodes.Append(SubGraphs[s]->Nodes);
			}
		}

		// Compute NumExportedEdges deterministically from the edge list: every edge
		// that survived culling contributes to both endpoints. Atomic increments
		// commute, so the counts are exact regardless of thread scheduling; nodes
		// that are not exported are never incremented and keep their default of 0.
		// (Also works without per-node Links, unlike the previous per-node recount.)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildSubGraphs::ExportedEdgeCounts);

			FNode* NodesData = Nodes.GetData();
			PCGExMT::ParallelOrSequential(
				NumEdges,
				[&](const int32 i)
				{
					const FEdge& Edge = Edges[i];
					if (!Edge.bValid)
					{
						return;
					}

					const int32 Start = static_cast<int32>(Edge.Start);
					const int32 End = static_cast<int32>(Edge.End);

					if (!NodeValid[Start] || !NodeValid[End])
					{
						return;
					}

					FPlatformAtomics::InterlockedIncrement(&NodesData[Start].NumExportedEdges);
					FPlatformAtomics::InterlockedIncrement(&NodesData[End].NumExportedEdges);
				});
		}
	}

	void FGraph::GetConnectedNodes(const int32 FromIndex, TArray<int32>& OutIndices, const int32 SearchDepth) const
	{
		const int32 NextDepth = SearchDepth - 1;
		const FNode& RootNode = Nodes[FromIndex];

		for (const FLink Lk : RootNode.Links)
		{
			const FEdge& Edge = Edges[Lk.Edge];
			if (!Edge.bValid)
			{
				continue;
			}

			int32 OtherIndex = Edge.Other(FromIndex);
			if (OutIndices.Contains(OtherIndex))
			{
				continue;
			}

			OutIndices.Add(OtherIndex);
			if (NextDepth > 0)
			{
				GetConnectedNodes(OtherIndex, OutIndices, NextDepth);
			}
		}
	}
}
