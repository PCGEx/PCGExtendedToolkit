// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExSubGraph.h"


#include "PCGExCoreSettingsCache.h"
#include "Algo/RemoveIf.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Metadata/PCGMetadata.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCache.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/Artifacts/PCGExCachedChain.h"
#include "Clusters/Artifacts/PCGExCachedFaceEnumerator.h"
#include "Core/PCGExOpStats.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/PCGExMathDistances.h"
#include "Sorting/PCGExSortingHelpers.h"

namespace PCGExGraphTask
{
	class FWriteSubGraphCluster final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FWriteSubGraphCluster)

		FWriteSubGraphCluster(const TSharedPtr<PCGExGraphs::FSubGraph>& InSubGraph)
			: FTask()
			  , SubGraph(InSubGraph)
		{
		}

		TSharedPtr<PCGExGraphs::FSubGraph> SubGraph;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			UPCGExClusterEdgesData* ClusterEdgesData = Cast<UPCGExClusterEdgesData>(SubGraph->EdgesDataFacade->GetOut());
			const TSharedPtr<PCGExGraphs::FGraph> ParentGraph = SubGraph->WeakParentGraph.Pin();
			if (!ParentGraph)
			{
				return;
			}
			PCGEX_MAKE_SHARED(NewCluster, PCGExClusters::FCluster, SubGraph->VtxDataFacade->Source, SubGraph->EdgesDataFacade->Source, ParentGraph->NodeIndexLookup)
			ClusterEdgesData->SetBoundCluster(NewCluster);

			SubGraph->BuildCluster(NewCluster.ToSharedRef());

			// Build pre-configured caches
			if (const TSharedPtr<PCGExGraphs::FGraphBuilder> Builder = SubGraph->GetBuilder())
			{
				if (Builder->OutputDetails)
				{
					// Native: FaceEnumerator
					if (Builder->OutputDetails->bPreBuildFaceEnumerator)
					{
						PCGExClusters::FClusterCacheBuildContext Context(NewCluster.ToSharedRef());
						Context.Projection = &Builder->OutputDetails->FaceEnumeratorProjection;

						if (PCGExClusters::IClusterCacheFactory* Factory = PCGExClusters::FClusterCacheRegistry::Get().GetFactory(
							PCGExClusters::FFaceEnumeratorCacheFactory::CacheKey))
						{
							if (TSharedPtr<PCGExClusters::ICachedClusterData> CachedData = Factory->Build(Context))
							{
								NewCluster->SetCachedData(Factory->GetCacheKey(), CachedData);
							}
						}
					}

					// Native: Node Chains
					if (Builder->OutputDetails->bPreBuildChains)
					{
						PCGExClusters::FClusterCacheBuildContext Context(NewCluster.ToSharedRef());

						if (PCGExClusters::IClusterCacheFactory* Factory = PCGExClusters::FClusterCacheRegistry::Get().GetFactory(
							PCGExClusters::FChainCacheFactory::CacheKey))
						{
							if (TSharedPtr<PCGExClusters::ICachedClusterData> CachedData = Factory->Build(Context))
							{
								NewCluster->SetCachedData(Factory->GetCacheKey(), CachedData);
							}
						}
					}
				}
			}
		}
	};
}

namespace PCGExGraphs
{
	void FSubGraph::Add(const FEdge& Edge)
	{
		Edges.Emplace(Edge.Index, Edge.H64U());
		if (Edge.IOIndex >= 0)
		{
			EdgesInIOIndices.Add(Edge.IOIndex);
		}
	}

	void FSubGraph::Shrink()
	{
		Nodes.Shrink();
		Edges.Shrink();
	}

	void FSubGraph::ComputeMinPointIndex(const TArray<FNode>& ParentNodes)
	{
		MinPointIndex = MAX_int32;
		for (const int32 NodeIdx : Nodes)
		{
			MinPointIndex = FMath::Min(MinPointIndex, ParentNodes[NodeIdx].PointIndex);
		}
	}

	void FSubGraph::BuildCluster(const TSharedRef<PCGExClusters::FCluster>& InCluster)
	{
		// Correct edge IO Index that has been overwritten during subgraph processing
		PCGEX_PARALLEL_FOR(FlattenedEdges.Num(), FlattenedEdges[i].IOIndex = -1;)
		InCluster->BuildFromSubgraphData(VtxDataFacade, EdgesDataFacade, FlattenedEdges, Nodes.Num());

		// Look into the cost of this

		//if (TaskManager) { NewCluster->ExpandEdges(TaskManager); }
		//else { NewCluster->GetExpandedEdges(true); }
	}

	int32 FSubGraph::GetFirstInIOIndex()
	{
		const TSet<int32>::TConstIterator It = EdgesInIOIndices.CreateConstIterator();
		return It ? *It : -1;
	}

	void FSubGraph::Compile(const TWeakPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<FGraphBuilder>& InBuilder)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::Compile);

		const TSharedPtr<FGraph> ParentGraph = WeakParentGraph.Pin();
		const TArray<FNode>& ParentGraphNodes = ParentGraph->Nodes;
		const TArray<FEdge>& ParentGraphEdges = ParentGraph->Edges;

		WeakBuilder = InBuilder;
		WeakTaskManager = TaskManager;

		// Try to create user context - if nullptr, skip PreCompile/PostCompile callbacks
		if (OnCreateContext)
		{
			UserContext = OnCreateContext();
		}

		const int32 NumEdges = Edges.Num();

		// When edges come from union graphs, node indices are non-deterministic due to parallel insertion.
		// Recompute sort keys using the now-remapped PointIndex values for deterministic ordering.
		if (InBuilder->bRequiresEdgeResort)
		{
			PCGEX_PARALLEL_FOR(
				Edges.Num(),
				const FEdge& E = ParentGraphEdges[Edges[i].Index];
				Edges[i].Key = PCGEx::H64U(ParentGraphNodes[E.Start].PointIndex, ParentGraphNodes[E.End].PointIndex);
				)
		}

		// Sorting establishes a deterministic output edge order; builders whose edge
		// insertion order is already deterministic can opt out of the cost entirely.
		if (InBuilder->bSortEdgeKeys || InBuilder->bRequiresEdgeResort)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::Compile::SortEdges);
			PCGExSortingHelpers::RadixSort(Edges);
		}

		FlattenedEdges.SetNumUninitialized(NumEdges);

		const UPCGBasePointData* InEdgeData = EdgesDataFacade->GetIn();

		EPCGPointNativeProperties AllocateProperties = InEdgeData ? InEdgeData->GetAllocatedProperties() : EPCGPointNativeProperties::None;
		AllocateProperties |= EPCGPointNativeProperties::MetadataEntry;

		if (InBuilder->OutputDetails->bWriteEdgePosition)
		{
			AllocateProperties |= EPCGPointNativeProperties::Transform;
		}

		if (InBuilder->OutputDetails->BasicEdgeSolidification.SolidificationAxis != EPCGExMinimalAxis::None)
		{
			AllocateProperties |= EPCGPointNativeProperties::Transform;
			AllocateProperties |= EPCGPointNativeProperties::BoundsMin;
			AllocateProperties |= EPCGPointNativeProperties::BoundsMax;
		}

		if (ParentGraph->bRefreshEdgeSeed || InBuilder->OutputDetails->bRefreshEdgeSeed)
		{
			AllocateProperties |= EPCGPointNativeProperties::Seed;
		}

		UPCGBasePointData* OutEdgeData = EdgesDataFacade->GetOut();
		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutEdgeData, NumEdges, AllocateProperties);
		EnumRemoveFlags(AllocateProperties, EPCGPointNativeProperties::MetadataEntry);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::Compile::BuildEdgesEntries);

			const TPCGValueRange<int64> OutMetadataEntries = OutEdgeData->GetMetadataEntryValueRange(false);
			UPCGMetadata* Metadata = OutEdgeData->MutableMetadata();

			// Entry keys handed out by AddEntryPlaceholder are sequential from the current
			// item count; this metadata is exclusively owned by this subgraph during
			// compilation, so the keys are synthesized directly instead of paying one
			// lock + shared atomic per edge. AddDelayedEntries sizes from the array we
			// pass, which keeps it in sync.
			const int64 EntryStart = Metadata->GetItemCountForChild();

			TArray<TTuple<int64, int64>> DelayedEntries;
			DelayedEntries.SetNumUninitialized(NumEdges);

			if (InEdgeData)
			{
				// We'll cherry pick existing edges
				TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::Compile::CherryPickInheritedEdges);

				TArray<int32> ReadEdgeIndices;
				TArray<int32> WriteEdgeIndices;

				ReadEdgeIndices.SetNumUninitialized(NumEdges);
				WriteEdgeIndices.SetNumUninitialized(NumEdges);

				const TConstPCGValueRange<int64> InMetadataEntries = InEdgeData->GetConstMetadataEntryValueRange();
				std::atomic<int32> WriteIndex(0);

				PCGExMT::ParallelOrSequential(
					NumEdges,
					[&](const int32 i)
					{
						const FEdge& OE = ParentGraphEdges[Edges[i].Index];

						// Hijack edge IOIndex to store original edge index in the flattened
						FlattenedEdges[i] = FEdge(i, ParentGraphNodes[OE.Start].PointIndex, ParentGraphNodes[OE.End].PointIndex, i, OE.Index);

						const int32 OriginalPointIndex = OE.PointIndex;
						int64 ParentEntry = PCGInvalidEntryKey;

						if (InMetadataEntries.IsValidIndex(OriginalPointIndex))
						{
							// Grab existing metadata entry & cache read/write indices
							ParentEntry = InMetadataEntries[OriginalPointIndex];
							const int32 LocalWriteIndex = WriteIndex.fetch_add(1, std::memory_order_relaxed);
							ReadEdgeIndices[LocalWriteIndex] = OriginalPointIndex;
							WriteEdgeIndices[LocalWriteIndex] = i;
						}

						const int64 Entry = EntryStart + i;
						OutMetadataEntries[i] = Entry;
						DelayedEntries[i] = MakeTuple(Entry, ParentEntry);
					});

				ReadEdgeIndices.SetNum(WriteIndex);
				WriteEdgeIndices.SetNum(WriteIndex);

				EdgesDataFacade->Source->InheritProperties(ReadEdgeIndices, WriteEdgeIndices, AllocateProperties);
			}
			else
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::Compile::CreatePoints);

				PCGExMT::ParallelOrSequential(
					NumEdges,
					[&](const int32 i)
					{
						const FEdge& E = ParentGraphEdges[Edges[i].Index];
						FlattenedEdges[i] = FEdge(i, ParentGraphNodes[E.Start].PointIndex, ParentGraphNodes[E.End].PointIndex, i, E.Index);

						const int64 Entry = EntryStart + i;
						OutMetadataEntries[i] = Entry;
						DelayedEntries[i] = MakeTuple(Entry, PCGInvalidEntryKey);
					});
			}

			Metadata->AddDelayedEntries(DelayedEntries);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrepareMetadata)

			MetadataDetails = InBuilder->GetMetadataDetails();
			const bool bHasUnionMetadata = (MetadataDetails && InBuilder && ParentGraph->HasAnyEdgeMetadata());

#define PCGEX_FOREACH_EDGE_METADATA(MACRO)\
MACRO(IsEdgeUnion, bool, false, IsUnion()) \
MACRO(IsSubEdge, bool, false, bIsSubEdge) \
MACRO(EdgeUnionSize, int32, 0, UnionSize)

#define PCGEX_EDGE_METADATA_DECL(_NAME, _TYPE, _DEFAULT, _ACCESSOR) if(bHasUnionMetadata && MetadataDetails->bWrite##_NAME){ _NAME##Buffer = EdgesDataFacade->GetWritable<_TYPE>(MetadataDetails->_NAME##AttributeName, _DEFAULT, true, PCGExData::EBufferInit::New); }
			PCGEX_FOREACH_EDGE_METADATA(PCGEX_EDGE_METADATA_DECL)
#undef PCGEX_EDGE_METADATA_DECL

			if (InBuilder->SourceEdgeFacades && ParentGraph->EdgesUnion)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(InitBlender)

				TArray<TSharedRef<PCGExData::FFacade>> EdgeSources = *InBuilder->SourceEdgeFacades;

				if (InBuilder->SourceEdgeFacades->Num() > 1)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(FindUniqueSourceIOIndices);

					TSet<int32> UniqueSourceIOIndices;

					// Many edges share the same union root; appending its IO set once per
					// root instead of once per edge avoids re-hashing the same entries.
					TSet<int32> VisitedRoots;

					for (const FEdge& E : FlattenedEdges)
					{
						const FGraphEdgeMetadata* EdgeMeta = ParentGraph->FindEdgeMetadata_Unsafe(E.IOIndex);
						if (const FGraphEdgeMetadata* RootEdgeMeta = EdgeMeta ? ParentGraph->FindEdgeMetadata_Unsafe(EdgeMeta->RootIndex) : nullptr)
						{
							bool bAlreadyVisited = false;
							VisitedRoots.Add(RootEdgeMeta->RootIndex, &bAlreadyVisited);
							if (bAlreadyVisited)
							{
								continue;
							}

							if (ParentGraph->EdgesUnion->Size(RootEdgeMeta->RootIndex) > 0)
							{
								UniqueSourceIOIndices.Append(ParentGraph->EdgesUnion->GetIOSet(RootEdgeMeta->RootIndex));
							}
						}
					}

					UniqueSourceIOIndices.Remove(-1);
					EdgeSources.SetNumUninitialized(Algo::RemoveIf(EdgeSources, [&UniqueSourceIOIndices](const TSharedRef<PCGExData::FFacade>& SrcIO)
					{
						return !UniqueSourceIOIndices.Contains(SrcIO->Source->IOIndex);
					}));
				}

				if (!EdgeSources.IsEmpty())
				{
					UnionBlender = MakeShared<PCGExBlending::FUnionBlender>(MetadataDetails->EdgesBlendingDetailsPtr, MetadataDetails->EdgesCarryOverDetails, PCGExMath::GetNoneDistances());
					UnionBlender->AddSources(EdgeSources, &PCGExClusters::Labels::ProtectedClusterAttributes);
					if (!UnionBlender->Init(TaskManager->GetContext(), EdgesDataFacade, ParentGraph->EdgesUnion))
					{
						// TODO : Log error
						return;
					}
				}
			}

			// Fetched once here rather than per-scope in CompileRange, where parallel
			// chunks would contend on the facade's buffer-map lock.
			EdgeEndpointsWriter = EdgesDataFacade->GetWritable<int64>(PCGExClusters::Labels::Attr_PCGExEdgeIdx, -1, false, PCGExData::EBufferInit::New);

			if (InBuilder->OutputDetails->bOutputEdgeLength)
			{
				if (!PCGExMetaHelpers::IsWritableAttributeName(InBuilder->OutputDetails->EdgeLengthName))
				{
					PCGE_LOG_C(Error, GraphAndLog, TaskManager->GetContext(), FTEXT("Invalid user-defined attribute name for Edge Length."));
				}
				else
				{
					EdgeLength = EdgesDataFacade->GetWritable<double>(InBuilder->OutputDetails->EdgeLengthName, 0, true, PCGExData::EBufferInit::New);
				}
			}
		}

		// PreCompile callback: after FlattenedEdges built, before CompileRange
		if (UserContext && OnPreCompile)
		{
			FSubGraphPreCompileData PreCompileData;
			PreCompileData.FlattenedEdges = FlattenedEdges;
			PreCompileData.EdgeKeys = Edges;
			PreCompileData.EdgesDataFacade = EdgesDataFacade;
			PreCompileData.VtxDataFacade = VtxDataFacade;
			PreCompileData.NumEdges = NumEdges;
			PreCompileData.NumNodes = Nodes.Num();
			OnPreCompile(*UserContext, PreCompileData);
		}

		if (NumEdges < 1024)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CompileAll);
			// Avoid contention when dealing with lots of small graphs
			CompileRange(PCGExMT::FScope(0, NumEdges));
			CompilationComplete();
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CompileParallel);
			PCGEX_ASYNC_SUBGROUP_REQ_CHKD_VOID(TaskManager, InParentHandle.Pin(), CompileSubGraph)

			CompileSubGraph->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->CompilationComplete();
			};

			CompileSubGraph->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				This->CompileRange(Scope);
			};

			CompileSubGraph->StartSubLoops(FlattenedEdges.Num(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		}
	}

	void FSubGraph::CompileRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSubGraph::CompileRange);

		const TSharedPtr<FGraph> ParentGraph = WeakParentGraph.Pin();
		const TSharedPtr<FGraphBuilder> Builder = WeakBuilder.Pin();

		if (!ParentGraph || !Builder)
		{
			return;
		}

		UPCGBasePointData* OutVtxData = VtxDataFacade->GetOut();
		UPCGBasePointData* OutEdgeData = EdgesDataFacade->GetOut();

		TConstPCGValueRange<FTransform> VtxTransforms = OutVtxData->GetConstTransformValueRange();

		TPCGValueRange<int32> EdgeSeeds = OutEdgeData->GetSeedValueRange(false);

		const bool bHasUnionMetadata = (MetadataDetails && ParentGraph->HasAnyEdgeMetadata());
		const FVector SeedOffset = FVector(EdgesDataFacade->Source->IOIndex);

		TArray<PCGExData::FWeightedPoint> WeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;

		if (UnionBlender)
		{
			UnionBlender->InitTrackers(Trackers);
		}

		PCGEX_SCOPE_LOOP(i)
		{
			const FEdge& E = FlattenedEdges[i];
			const int32 EdgeIndex = E.Index; // TODO : This is now i, anyway?

			const int32 Start = E.Start;
			const int32 End = E.End;

			PCGExData::FMutablePoint EdgePt = EdgesDataFacade->GetOutPoint(EdgeIndex);

			if (bHasUnionMetadata)
			{
				const FGraphEdgeMetadata* EdgeMeta = ParentGraph->FindEdgeMetadata_Unsafe(E.IOIndex);
				if (const FGraphEdgeMetadata* RootEdgeMeta = EdgeMeta ? ParentGraph->FindEdgeMetadata_Unsafe(EdgeMeta->RootIndex) : nullptr)
				{
					if (UnionBlender && ParentGraph->EdgesUnion && ParentGraph->EdgesUnion->Size(RootEdgeMeta->RootIndex) > 0)
					{
						UnionBlender->MergeSingle(EdgeIndex, ParentGraph->EdgesUnion, RootEdgeMeta->RootIndex, WeightedPoints, Trackers);
					}

					// TODO : Add Sub-edge edge (is the result of a subdivision + merge)

					if (IsEdgeUnionBuffer)
					{
						IsEdgeUnionBuffer->SetValue(EdgeIndex, RootEdgeMeta->IsUnion() || EdgeMeta->IsUnion());
					}
					if (IsSubEdgeBuffer)
					{
						IsSubEdgeBuffer->SetValue(EdgeIndex, RootEdgeMeta->bIsSubEdge || EdgeMeta->bIsSubEdge);
					}
					if (EdgeUnionSizeBuffer)
					{
						EdgeUnionSizeBuffer->SetValue(EdgeIndex, EdgeMeta != RootEdgeMeta ? RootEdgeMeta->UnionSize + EdgeMeta->UnionSize : RootEdgeMeta->UnionSize);
					}
				}
			}

			EdgeEndpointsWriter->SetValue(EdgeIndex, PCGEx::H64(Start, End));

			if (Builder->OutputDetails->bWriteEdgePosition)
			{
				Builder->OutputDetails->BasicEdgeSolidification.Mutate(EdgePt, VtxDataFacade->GetOutPoint(Start), VtxDataFacade->GetOutPoint(End), Builder->OutputDetails->EdgePosition);
			}

			if (EdgeLength)
			{
				EdgeLength->SetValue(EdgeIndex, FVector::Dist(VtxTransforms[Start].GetLocation(), VtxTransforms[End].GetLocation()));
			}

			if (EdgeSeeds[EdgeIndex] == 0 || ParentGraph->bRefreshEdgeSeed)
			{
				EdgeSeeds[EdgeIndex] = PCGExRandomHelpers::ComputeSpatialSeed(EdgePt.GetLocation(), SeedOffset);
			}
		}
	}

	void FSubGraph::CompilationComplete()
	{
		UnionBlender.Reset();

		const TSharedPtr<PCGExMT::FTaskManager>& TaskManager = WeakTaskManager.Pin();
		const TSharedPtr<FGraph> ParentGraph = WeakParentGraph.Pin();
		if (!TaskManager || !TaskManager->IsAvailable() || !ParentGraph)
		{
			return;
		}

		PCGEX_SHARED_THIS_DECL

		if (PCGEX_CORE_SETTINGS.bCacheClusters && ParentGraph->bBuildClusters)
		{
			if (Cast<UPCGExClusterEdgesData>(EdgesDataFacade->Source->GetOut()))
			{
				PCGEX_LAUNCH(PCGExGraphTask::FWriteSubGraphCluster, ThisPtr)
			}
		}

		// Context-based PostCompile callback (new)
		if (UserContext && OnPostCompile)
		{
			OnPostCompile(*UserContext, ThisPtr.ToSharedRef());
		}

		// Legacy callback (for backwards compatibility)
		if (OnSubGraphPostProcess)
		{
			OnSubGraphPostProcess(ThisPtr.ToSharedRef());
		}

		// Clean up user context
		UserContext.Reset();

		EdgesDataFacade->WriteFastest(TaskManager);
	}

#undef PCGEX_FOREACH_EDGE_METADATA
}
