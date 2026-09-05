// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExGraphBuilder.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExSubGraph.h"
#include "Sorting/PCGExSortingHelpers.h"

namespace PCGExGraphTask
{
	class FCompileGraph final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FCompileGraph)

		FCompileGraph(const TSharedPtr<PCGExGraphs::FGraphBuilder>& InGraphBuilder, const bool bInWriteNodeFacade, const PCGExGraphs::FGraphMetadataDetails* InMetadataDetails = nullptr)
			: FTask()
			  , Builder(InGraphBuilder)
			  , bWriteNodeFacade(bInWriteNodeFacade)
			  , MetadataDetails(InMetadataDetails)
		{
		}

		TSharedPtr<PCGExGraphs::FGraphBuilder> Builder;
		const bool bWriteNodeFacade = false;
		const PCGExGraphs::FGraphMetadataDetails* MetadataDetails = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::ExecuteTask);
			Builder->Compile(TaskManager, bWriteNodeFacade, MetadataDetails);
		}
	};
}

namespace PCGExGraphs
{
	FGraphBuilder::FGraphBuilder(const TSharedRef<PCGExData::FFacade>& InNodeDataFacade, const FPCGExGraphBuilderDetails* InDetails)
		: OutputDetails(InDetails)
		  , NodeDataFacade(InNodeDataFacade)
	{
		PCGEX_SHARED_CONTEXT_VOID(NodeDataFacade->Source->GetContextHandle())

		const UPCGBasePointData* NodePointData = NodeDataFacade->Source->GetOutIn();
		PairId = NodeDataFacade->Source->Tags->Set<int64>(PCGExClusters::Labels::TagStr_PCGExCluster, NodePointData->GetUniqueID());


		// We initialize from the number of output point if it's greater than 0 at init time
		// Otherwise, init with input points

		const int32 NumOutPoints = NodeDataFacade->Source->GetOut() ? NodeDataFacade->Source->GetNum(PCGExData::EIOSide::Out) : 0;
		int32 InitialNumNodes = 0;

		if (NumOutPoints != 0)
		{
			NodePointsTransforms = NodeDataFacade->Source->GetOut()->GetConstTransformValueRange();
			InitialNumNodes = NumOutPoints;
		}
		else
		{
			NodePointsTransforms = NodeDataFacade->Source->GetIn()->GetConstTransformValueRange();
			InitialNumNodes = NodeDataFacade->Source->GetNum(PCGExData::EIOSide::In);
		}

		check(InitialNumNodes > 0)

		Graph = MakeShared<FGraph>(InitialNumNodes);

		Graph->bBuildClusters = InDetails->WantsClusters();
		Graph->bRefreshEdgeSeed = OutputDetails->bRefreshEdgeSeed;

		EdgesIO = MakeShared<PCGExData::FPointIOCollection>(SharedContext.Get());
		EdgesIO->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;
	}

	void FGraphBuilder::CompileAsync(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const bool bWriteNodeFacade, const FGraphMetadataDetails* MetadataDetails)
	{
		TaskManager = InTaskManager;
		PCGEX_SHARED_THIS_DECL
		PCGEX_LAUNCH(PCGExGraphTask::FCompileGraph, ThisPtr, bWriteNodeFacade, MetadataDetails)
	}

	void FGraphBuilder::Compile(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const bool bWriteNodeFacade, const FGraphMetadataDetails* MetadataDetails)
	{
		check(!bCompiling)

		// Nodes are output with final positions at compile time, so graph data is fully allocated even when much of it is later pruned.

		bCompiling = true;
		TaskManager = InTaskManager;
		MetadataDetailsPtr = MetadataDetails;
		bWriteVtxDataFacadeWithCompile = bWriteNodeFacade;

		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphBuilder::Compile);

		TArray<FNode>& Nodes = Graph->Nodes;
		const int32 NumNodes = Nodes.Num();

		NodeIndexLookup = MakeShared<PCGEx::FIndexLookup>(NumNodes); // Likely larger than exported size; required for compilation.
		Graph->NodeIndexLookup = NodeIndexLookup;

		TArray<int32> InternalValidNodes;
		TArray<int32>& ValidNodes = InternalValidNodes;
		if (OutputNodeIndices)
		{
			ValidNodes = *OutputNodeIndices.Get();
		}

		// Building subgraphs isolate connected edge clusters
		// and invalidate roaming (isolated) nodes
		Graph->BuildSubGraphs(*OutputDetails, ValidNodes);

		if (Graph->SubGraphs.IsEmpty())
		{
			bCompiledSuccessfully = false;
			if (OnCompilationEndCallback)
			{
				PCGEX_SHARED_THIS_DECL
				OnCompilationEndCallback(ThisPtr.ToSharedRef(), bCompiledSuccessfully);
			}
			return;
		}

		NodeDataFacade->Source->ClearCachedKeys(); //Ensure fresh keys later on


		const int32 NumValidNodes = ValidNodes.Num();
		bool bHasInvalidNodes = NumValidNodes != NumNodes;

		TArray<int32> ReadIndices;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::PrunePoints);

			const UPCGBasePointData* InNodeData = NodeDataFacade->GetIn();
			UPCGBasePointData* OutNodeData = NodeDataFacade->GetOut();

			if (InNodeData && bInheritNodeData)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::PrunePoints::Inherit);

				ReadIndices.SetNumUninitialized(NumValidNodes);

				// In order to inherit from node data
				// both input & output must be valid
				check(!InNodeData->IsEmpty())
				check(InNodeData->GetNumPoints() >= NumValidNodes)

				const bool bOutputIsSameAsInput = !bHasInvalidNodes && NumValidNodes == InNodeData->GetNumPoints() && NumValidNodes == OutNodeData->GetNumPoints();

				if (!bOutputIsSameAsInput)
				{
					// Build & remap new point count to node topology
					PCGEX_PARALLEL_FOR(
						NumValidNodes,
						FNode& Node = Nodes[ValidNodes[i]];
						ReadIndices[i] = Node.PointIndex; // { NewIndex : InheritedIndex }
						Node.PointIndex = i;              // Update node point index
						)

					// Truncate output if need be
					OutNodeData->SetNumPoints(NumValidNodes);
					// Copy input to outputs to carry over the right values on the outgoing points
					NodeDataFacade->Source->InheritProperties(ReadIndices);
				}
			}
			else
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::PrunePoints::New);

				// We don't have to inherit points, this sounds great
				// However it makes things harder for us because we need to enforce a deterministic layout for other cluster nodes
				// We make the assumption that if we don't inherit points, we've introduced new one nodes & edges from different threads
				// The cheap way to make things deterministic is to sort nodes by spatial position

				// Rough check to make sure we won't have a PointIndex that's outside the desired range
				check(NodePointsTransforms.Num() >= NumNodes)

				// We must have an output size that's at least equal to the number of nodes we have as well, to do the re-order
				check(OutNodeData->GetNumPoints() >= NumNodes)

				// Init array of indice as a valid order range first, will be truncated later.
				// We save a bit of memory by re-using it
				PCGExArrayHelpers::ArrayOfIndices(ReadIndices, OutNodeData->GetNumPoints());

				if (bNodesPreSorted)
				{
					// Nodes already sorted at Collapse() time - just remap without re-sorting
					PCGEX_PARALLEL_FOR(
						NumValidNodes,
						const int32 Idx = ValidNodes[i];
						FNode& Node = Nodes[Idx];
						ReadIndices[i] = Node.PointIndex;
						Node.PointIndex = i;
						)
				}
				else
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::Sort);

					const int32 N = NumValidNodes;
					TArray<PCGEx::FIndexKey> MortonHash;
					MortonHash.SetNumUninitialized(N);

					PCGEX_PARALLEL_FOR(
						N,
						const int32 Idx = ValidNodes[i];
						MortonHash[i] = PCGEx::FIndexKey(Idx, PCGEx::MH64(NodePointsTransforms[Idx].GetLocation()));
						)

					PCGExSortingHelpers::RadixSort(MortonHash);

					PCGEX_PARALLEL_FOR(
						NumValidNodes,
						const int32 Idx = MortonHash[i].Index;
						ValidNodes[i] = Idx;
						FNode& Node = Nodes[Idx];
						ReadIndices[i] = Node.PointIndex;
						Node.PointIndex = i;
						)
				}

				// There is no points to inherit from; meaning we need to reorder the existing data
				// because it's likely to be fragmented
				PCGExPointArrayDataHelpers::Reorder(OutNodeData, ReadIndices);

				// Truncate output to the number of nodes
				OutNodeData->SetNumPoints(NumValidNodes);
			}
		}

		////////////
		//  At this point, OutPointData must be up-to-date
		//  Transforms & Metadata entry must be final and match the Nodes.PointIndex
		//	Subgraph compilation rely on it.
		///////////

		if (OutputPointIndices && OutputPointIndices->Num() == NumValidNodes)
		{
			// Reorder output indices if provided
			// Needed for delaunay etc that rely on original indices to identify sites etc
			TArray<int32>& OutputPointIndicesRef = *OutputPointIndices.Get();
			for (int32 i = 0; i < NumValidNodes; i++)
			{
				OutputPointIndicesRef[i] = ReadIndices[i];
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FCompileGraph::VtxEndpoints);

			const TSharedPtr<PCGExData::TBuffer<int64>> VtxEndpointWriter = NodeDataFacade->GetWritable<int64>(PCGExClusters::Labels::Attr_PCGExVtxIdx, 0, false, PCGExData::EBufferInit::New);
			const TSharedPtr<PCGExData::TArrayBuffer<int64>> ElementsVtxEndpointWriter = StaticCastSharedPtr<PCGExData::TArrayBuffer<int64>>(VtxEndpointWriter);

			TArray<int64>& VtxEndpoints = *ElementsVtxEndpointWriter->GetOutValues().Get();
			PCGEX_PARALLEL_FOR(
				ValidNodes.Num(),
				const FNode& Node = Nodes[ValidNodes[i]];
				VtxEndpoints[Node.PointIndex] = PCGEx::H64(Node.PointIndex, Node.NumExportedEdges);
				)
		}

		if (MetadataDetails && Graph->HasAnyNodeMetadata())
		{
#define PCGEX_FOREACH_NODE_METADATA(MACRO)\
			MACRO(IsPointUnion, bool, false, IsUnion()) \
			MACRO(PointUnionSize, int32, 0, UnionSize) \
			MACRO(IsIntersector, bool, false, IsIntersector()) \
			MACRO(Crossing, bool, false, IsCrossing())
#define PCGEX_NODE_METADATA_DECL(_NAME, _TYPE, _DEFAULT, _ACCESSOR) const TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Buffer = MetadataDetails->bWrite##_NAME ? NodeDataFacade->GetWritable<_TYPE>(MetadataDetails->_NAME##AttributeName, _DEFAULT, true, PCGExData::EBufferInit::New) : nullptr;
#define PCGEX_NODE_METADATA_OUTPUT(_NAME, _TYPE, _DEFAULT, _ACCESSOR) if(_NAME##Buffer){_NAME##Buffer->SetValue(PointIndex, NodeMeta->_ACCESSOR);}

			PCGEX_FOREACH_NODE_METADATA(PCGEX_NODE_METADATA_DECL)

			PCGExMT::ParallelOrSequential(
				NumValidNodes,
				[&](const int32 i)
				{
					// Index through ValidNodes (like VtxEndpoints above): metadata is keyed by original
					// node index, and survivors are sparse/reordered once isolated nodes are pruned.
					const int32 NodeIndex = ValidNodes[i];
					const FGraphNodeMetadata* NodeMeta = Graph->FindNodeMetadata_Unsafe(NodeIndex);
					if (NodeMeta)
					{
						const int32 PointIndex = Nodes[NodeIndex].PointIndex;
						PCGEX_FOREACH_NODE_METADATA(PCGEX_NODE_METADATA_OUTPUT)
					}
				});

#undef PCGEX_FOREACH_NODE_METADATA
#undef PCGEX_NODE_METADATA_DECL
#undef PCGEX_NODE_METADATA_OUTPUT
		}

		bCompiledSuccessfully = true;

		// Subgraphs
		if (Graph->SubGraphs.Num() > 1)
		{
			// Sort subgraphs by minimum PointIndex for deterministic cluster IOIndex assignment
			// Ensures consistent cluster ordering based on spatial position
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SortSubGraphs);

				for (const TSharedRef<FSubGraph>& SubGraph : Graph->SubGraphs)
				{
					SubGraph->ComputeMinPointIndex(Nodes);
				}

				Graph->SubGraphs.Sort([](const TSharedRef<FSubGraph>& A, const TSharedRef<FSubGraph>& B)
				{
					return A->MinPointIndex < B->MinPointIndex;
				});
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CreateEdgeData)
			for (int i = 0; i < Graph->SubGraphs.Num(); i++)
			{
				const TSharedPtr<FSubGraph>& SubGraph = Graph->SubGraphs[i];

				check(!SubGraph->Edges.IsEmpty())

				TSharedPtr<PCGExData::FPointIO> EdgeIO;

				if (const int32 IOIndex = SubGraph->GetFirstInIOIndex();
					SubGraph->EdgesInIOIndices.Num() == 1 && SourceEdgeFacades && SourceEdgeFacades->IsValidIndex(IOIndex))
				{
					// Don't grab original point IO if we have metadata.
					EdgeIO = EdgesIO->Emplace_GetRef<UPCGExClusterEdgesData>((*SourceEdgeFacades)[IOIndex]->Source, PCGExData::EIOInit::New);
				}
				else
				{
					EdgeIO = EdgesIO->Emplace_GetRef<UPCGExClusterEdgesData>(PCGExData::EIOInit::New);
				}

				if (!EdgeIO)
				{
					return;
				}

				EdgeIO->IOIndex = i;

				SubGraph->UID = EdgeIO->GetOut()->GetUniqueID();

				// Legacy callback
				SubGraph->OnSubGraphPostProcess = OnSubGraphPostProcess;

				// Context-based callbacks
				SubGraph->OnCreateContext = OnCreateContext;
				SubGraph->OnPreCompile = OnPreCompile;
				SubGraph->OnPostCompile = OnPostCompile;

				SubGraph->VtxDataFacade = NodeDataFacade;
				SubGraph->EdgesDataFacade = MakeShared<PCGExData::FFacade>(EdgeIO.ToSharedRef());

				PCGExClusters::Helpers::MarkClusterEdges(EdgeIO, PairId);
			}
		}

		PCGExClusters::Helpers::MarkClusterVtx(NodeDataFacade->Source, PairId);

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, BatchCompileSubGraphs)

		BatchCompileSubGraphs->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnCompilationEnd();
		};

		BatchCompileSubGraphs->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, WeakGroup = BatchCompileSubGraphs](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			const TSharedPtr<FSubGraph> SubGraph = This->Graph->SubGraphs[Index];
			SubGraph->Compile(WeakGroup, This->TaskManager, This);
		};

		BatchCompileSubGraphs->StartIterations(Graph->SubGraphs.Num(), 1, false);
	}

	void FGraphBuilder::OnCompilationEnd()
	{
		TSharedRef<FGraphBuilder> Self = SharedThis(this);

		if (bWriteVtxDataFacadeWithCompile)
		{
			if (OnCompilationEndCallback)
			{
				if (!bCompiledSuccessfully)
				{
					OnCompilationEndCallback(Self, false);
				}
				else
				{
					NodeDataFacade->WriteBuffers(TaskManager, [PCGEX_ASYNC_THIS_CAPTURE]()
					{
						PCGEX_ASYNC_THIS
						This->OnCompilationEndCallback(This.ToSharedRef(), true);
					});
				}
			}
			else if (bCompiledSuccessfully)
			{
				NodeDataFacade->WriteFastest(TaskManager);
			}
		}
		else if (OnCompilationEndCallback)
		{
			OnCompilationEndCallback(Self, bCompiledSuccessfully);
		}
	}

	void FGraphBuilder::StageEdgesOutputs() const
	{
		EdgesIO->StageOutputs();
	}

	void FGraphBuilder::MoveEdgesOutputs(const TSharedPtr<PCGExData::FPointIOCollection>& To, const int32 IndexOffset) const
	{
		for (const TSharedPtr<PCGExData::FPointIO>& IO : EdgesIO->Pairs)
		{
			const int32 DesiredIndex = IO->IOIndex + IndexOffset;
			To->Add(IO);
			IO->IOIndex = DesiredIndex;
		}

		EdgesIO->Pairs.Empty();
	}
}
