// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/Union/PCGExUnionProcessor.h"


#include "PCGExCoreSettingsCache.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Data/PCGExPointIO.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExProxyDataBlending.h"
#include "Data/PCGExData.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/Union/PCGExIntersections.h"

namespace PCGExGraphs
{
	FUnionProcessor::FUnionProcessor(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade> InUnionDataFacade, TSharedRef<FUnionGraph> InUnionGraph, FPCGExPointPointIntersectionDetails InPointPointIntersectionSettings, FPCGExBlendingDetails InDefaultPointsBlending, FPCGExBlendingDetails InDefaultEdgesBlending)
		: Context(InContext), UnionDataFacade(InUnionDataFacade), UnionGraph(InUnionGraph), PointPointIntersectionDetails(InPointPointIntersectionSettings), DefaultPointsBlendingDetails(InDefaultPointsBlending), DefaultEdgesBlendingDetails(InDefaultEdgesBlending)
	{
	}

	FUnionProcessor::~FUnionProcessor()
	{
	}

	void FUnionProcessor::InitPointEdge(const FPCGExPointEdgeIntersectionDetails& InDetails, const bool bUseCustom, const FPCGExBlendingDetails* InOverride)
	{
		bDoPointEdge = true;
		PointEdgeIntersectionDetails = InDetails;
		bUseCustomPointEdgeBlending = bUseCustom;
		if (InOverride) { CustomPointEdgeBlendingDetails = *InOverride; }
	}

	void FUnionProcessor::InitEdgeEdge(const FPCGExEdgeEdgeIntersectionDetails& InDetails, const bool bUseCustom, const FPCGExBlendingDetails* InOverride)
	{
		bDoEdgeEdge = true;
		EdgeEdgeIntersectionDetails = InDetails;
		EdgeEdgeIntersectionDetails.Init();
		bUseCustomEdgeEdgeBlending = bUseCustom;
		if (InOverride) { CustomEdgeEdgeBlendingDetails = *InOverride; }
	}

	bool FUnionProcessor::StartExecution(const TArray<TSharedRef<PCGExData::FFacade>>& InFacades, const FPCGExGraphBuilderDetails& InBuilderDetails)
	{
		BuilderDetails = InBuilderDetails;

		const int32 NumUnionNodes = UnionGraph->Nodes.Num();
		if (NumUnionNodes == 0)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Union graph is empty. Something is likely corrupted."));
			return false;
		}

		UnionGraph->Collapse();
		Context->SetState(States::State_ProcessingUnion);

		const TSharedPtr<PCGExBlending::FUnionBlender> TypedBlender = MakeShared<PCGExBlending::FUnionBlender>(
			&DefaultPointsBlendingDetails, VtxCarryOverDetails, PointPointIntersectionDetails.FuseDetails.GetDistances());

		UnionBlender = TypedBlender;

		TypedBlender->AddSources(InFacades, &PCGExClusters::Labels::ProtectedClusterAttributes);

		UPCGBasePointData* MutablePoints = UnionDataFacade->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumUnionNodes, UnionBlender->GetAllocatedProperties()); // TODO : Proper Allocation

		if (!TypedBlender->Init(Context, UnionDataFacade, UnionGraph->NodesUnion)) { return false; }

		PCGEX_ASYNC_GROUP_CHKD(Context->GetTaskManager(), ProcessNodesGroup)

		ProcessNodesGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionBlender.Reset();
			This->OnNodesProcessingComplete();
		};

		ProcessNodesGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			const TSharedPtr<PCGExBlending::IUnionBlender> Blender = This->UnionBlender;

			TArray<PCGExData::FWeightedPoint> WeightedPoints;
			TArray<PCGEx::FOpStats> Trackers;
			Blender->InitTrackers(Trackers);

			UPCGBasePointData* OutPoints = This->UnionDataFacade->GetOut();
			TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange(false);

			PCGEX_SCOPE_LOOP(Index)
			{
				TSharedPtr<FUnionNode> UnionNode = This->UnionGraph->Nodes[Index];

				//const PCGMetadataEntryKey Key = OutPoints[i].MetadataEntry;
				//OutPoints[Index] = UnionNode->Point; // Copy "original" point properties, in case  there's only one

				//FPCGPoint& Point = OutPoints[Index];
				//Point.MetadataEntry = Key; // Restore key

				OutTransforms[Index].SetLocation(UnionNode->GetCenter());
				Blender->MergeSingle(Index, WeightedPoints, Trackers);
			}
		};

		ProcessNodesGroup->StartSubLoops(NumUnionNodes, PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2, false);


		return true;
	}

	void FUnionProcessor::OnNodesProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::OnNodesProcessingComplete);

		UnionBlender.Reset();

		bRunning = true;


		GraphMetadataDetails.Update(Context, PointPointIntersectionDetails);
		GraphMetadataDetails.Update(Context, PointEdgeIntersectionDetails);
		GraphMetadataDetails.Update(Context, EdgeEdgeIntersectionDetails);
		GraphMetadataDetails.EdgesBlendingDetailsPtr = bUseCustomEdgeEdgeBlending ? &CustomEdgeEdgeBlendingDetails : &DefaultEdgesBlendingDetails;
		GraphMetadataDetails.EdgesCarryOverDetails = EdgesCarryOverDetails;

		GraphBuilder = MakeShared<FGraphBuilder>(UnionDataFacade, &BuilderDetails);
		GraphBuilder->bInheritNodeData = false;
		GraphBuilder->bRequiresEdgeResort = false; // All insertion is sequential → deterministic node ordering
		GraphBuilder->bNodesPreSorted = UnionGraph->bNodesSorted;
		GraphBuilder->SourceEdgeFacades = SourceEdgesIO;
		GraphBuilder->Graph->NodesUnion = UnionGraph->NodesUnion;
		GraphBuilder->Graph->EdgesUnion = UnionGraph->EdgesUnion;

		GraphBuilder->Graph->AdoptEdges(UnionGraph->Edges);

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), WriteMetadataTask);
		WriteMetadataTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionDataFacade->Flush();
			This->InternalStartExecution();
		};

		UnionDataFacade->WriteBuffersAsCallbacks(WriteMetadataTask);

		WriteMetadataTask->AddSimpleCallback([PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionGraph->WriteNodeMetadata(This->GraphBuilder->Graph);
		});

		WriteMetadataTask->AddSimpleCallback([PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionGraph->WriteEdgeMetadata(This->GraphBuilder->Graph);
		});

		WriteMetadataTask->StartSimpleCallbacks();
	}

	void FUnionProcessor::InternalStartExecution()
	{
		if (GraphBuilder->Graph->Edges.Num() <= 1) { CompileFinalGraph(); } // Nothing to be found
		else if (bDoPointEdge) { FindPointEdgeIntersections(); }
		else if (bDoEdgeEdge) { FindEdgeEdgeIntersections(); }
		else { CompileFinalGraph(); }
	}

	bool FUnionProcessor::Execute()
	{
		if (!bRunning || Context->IsState(States::State_ProcessingUnion)) { return false; }

		PCGEX_ON_ASYNC_STATE_READY(States::State_ProcessingPointEdgeIntersections)
		{
			if (bDoEdgeEdge) { FindEdgeEdgeIntersections(); }
			else { CompileFinalGraph(); }
			return false;
		}

		PCGEX_ON_ASYNC_STATE_READY(States::State_ProcessingEdgeEdgeIntersections)
		{
			CompileFinalGraph();
			return false;
		}

		PCGEX_ON_ASYNC_STATE_READY(States::State_WritingClusters)
		{
			return true;
		}

		return true;
	}

#pragma region PointEdge


	void FUnionProcessor::FindPointEdgeIntersections()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::FindPointEdgeIntersections);

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), FindPointEdgeGroup)

		PointEdgeIntersections = MakeShared<FPointEdgeIntersections>(GraphBuilder->Graph, UnionDataFacade->Source, &PointEdgeIntersectionDetails);

		Context->SetState(States::State_ProcessingPointEdgeIntersections);

		// Init point octree
		(void)PointEdgeIntersections->PointIO->GetOutIn()->GetPointOctree();

		FindPointEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnPointEdgeIntersectionsFound();
		};

		FindPointEdgeGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->PointEdgeIntersections->Init(Loops);
		};

		FindPointEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FindPointEdgeIntersections::ScopeLoop)

			PCGEX_ASYNC_THIS
			TSharedPtr<FPointEdgeProxy> EdgeProxy = MakeShared<FPointEdgeProxy>();
			TSharedPtr<FPointEdgeIntersections> PEI = This->PointEdgeIntersections;

			TArray<TSharedPtr<FPointEdgeProxy>>& ScopedEdges = PEI->ScopedEdges->Get_Ref(Scope);

			int32& PENum_Ref = This->PENum;
			TArray<FEdge>& GraphEdges = This->GraphBuilder->Graph->Edges;

#define PCGEX_FOUND_PE \
				ScopedEdges.Add(EdgeProxy); \
				FPlatformAtomics::InterlockedAdd(&PENum_Ref, EdgeProxy->CollinearPoints.Num() + 1); \
				GraphEdges[Index].bValid = 0; \
				EdgeProxy->CollinearPoints.Sort([](const FPESplit& A, const FPESplit& B) { return A.Time < B.Time; }); \
				EdgeProxy = MakeShared<FPointEdgeProxy>();

			// Self-intersection bool is hoisted out of the loop -- FindCollinearNodes handles
			// the branching internally (computing RootIOIndices only when needed).
			const bool bSelfIntersect = PEI->Details->bEnableSelfIntersection;
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!PEI->InitProxy(EdgeProxy, Index)) { continue; }
				FindCollinearNodes(PEI, EdgeProxy, bSelfIntersect);
				if (!EdgeProxy->IsEmpty()) { PCGEX_FOUND_PE }
			}
#undef PCGEX_FOUND_PE
		};

		FindPointEdgeGroup->StartSubLoops(GraphBuilder->Graph->Edges.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}


	void FUnionProcessor::OnPointEdgeIntersectionsFound()
	{
		if (!PointEdgeIntersections)
		{
			OnPointEdgeIntersectionsComplete();
			return;
		}

		// InsertEdges collapses ScopedEdges into Edges, then creates sub-edges in the graph.
		// Must only be called once -- a second call would find duplicates via InsertEdge_Unsafe
		// and incorrectly increment UnionSize on existing metadata.
		PointEdgeIntersections->InsertEdges();

		if (PointEdgeIntersections->Edges.IsEmpty())
		{
			OnPointEdgeIntersectionsComplete();
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), BlendPointEdgeGroup)

		UnionDataFacade->Source->ClearCachedKeys();

		MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
		MetadataBlender->SetTargetData(UnionDataFacade);
		MetadataBlender->SetSourceData(UnionDataFacade, PCGExData::EIOSide::Out);

		if (!MetadataBlender->Init(Context, bUseCustomPointEdgeBlending ? CustomPointEdgeBlendingDetails : DefaultPointsBlendingDetails, &PCGExClusters::Labels::ProtectedClusterAttributes))
		{
			// Fail
			Context->CancelExecution(FString("Error initializing Point/Edge blending"));
			return;
		}

		BlendPointEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnPointEdgeIntersectionsComplete();
		};

		BlendPointEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			if (!This->MetadataBlender) { return; }
			const TSharedRef<PCGExBlending::FMetadataBlender> Blender = This->MetadataBlender.ToSharedRef();

			PCGEX_SCOPE_LOOP(Index)
			{
				// TODO
			}
		};

		BlendPointEdgeGroup->StartSubLoops(PointEdgeIntersections->Edges.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnPointEdgeIntersectionsComplete()
	{
		PointEdgeIntersections.Reset();
		if (MetadataBlender) { UnionDataFacade->WriteFastest(Context->GetTaskManager()); }
	}

#pragma endregion

#pragma region EdgeEdge

	void FUnionProcessor::FindEdgeEdgeIntersections()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::FindEdgeEdgeIntersections);

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), FindEdgeEdgeGroup)

		EdgeEdgeIntersections = MakeShared<FEdgeEdgeIntersections>(GraphBuilder->Graph, UnionGraph, UnionDataFacade->Source, &EdgeEdgeIntersectionDetails);

		Context->SetState(States::State_ProcessingEdgeEdgeIntersections);

		FindEdgeEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnEdgeEdgeIntersectionsFound();
		};

		FindEdgeEdgeGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->EdgeEdgeIntersections->Init(Loops);
		};

		FindEdgeEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			int32& EENum_Ref = This->EENum;
			TArray<FEdge>& GraphEdges = This->GraphBuilder->Graph->Edges;

#define PCGEX_FOUND_EE \
				ScopedEdges.Add(EdgeProxy); \
				GraphEdges[Index].bValid = 0; \
				FPlatformAtomics::InterlockedAdd(&EENum_Ref, EdgeProxy->Crossings.Num()); \
				EdgeProxy = MakeShared<FEdgeEdgeProxy>();


			const TSharedRef<FEdgeEdgeIntersections> EEI = This->EdgeEdgeIntersections.ToSharedRef();
			TArray<TSharedPtr<FEdgeEdgeProxy>>& ScopedEdges = EEI->ScopedEdges->Get_Ref(Scope);

			TSharedPtr<FEdgeEdgeProxy> EdgeProxy = MakeShared<FEdgeEdgeProxy>();

			// Same pattern as PE: self-intersection bool hoisted, FindOverlappingEdges handles internally.
			const bool bSelfIntersect = EEI->Details->bEnableSelfIntersection;
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!EEI->InitProxy(EdgeProxy, Index)) { continue; }
				FindOverlappingEdges(EEI, EdgeProxy, bSelfIntersect);
				if (!EdgeProxy->IsEmpty()) { PCGEX_FOUND_EE }
			}

#undef PCGEX_FOUND_EE
		};


		FindEdgeEdgeGroup->StartSubLoops(GraphBuilder->Graph->Edges.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnEdgeEdgeIntersectionsFound()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::OnEdgeEdgeIntersectionsFound);

		if (!EdgeEdgeIntersections || !EdgeEdgeIntersections->InsertNodes(EENum / 2))
		{
			OnEdgeEdgeIntersectionsComplete();
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), BlendEdgeEdgeGroup)

		EdgeEdgeIntersections->InsertEdges();
		UnionDataFacade->Source->ClearCachedKeys();

		MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
		MetadataBlender->SetTargetData(UnionDataFacade);
		MetadataBlender->SetSourceData(UnionDataFacade, PCGExData::EIOSide::Out);

		if (!MetadataBlender->Init(Context, bUseCustomEdgeEdgeBlending ? CustomEdgeEdgeBlendingDetails : DefaultPointsBlendingDetails, &PCGExClusters::Labels::ProtectedClusterAttributes))
		{
			// Fail
			Context->CancelExecution(FString("Error initializing Edge/Edge blending"));
			return;
		}

		BlendEdgeEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnEdgeEdgeIntersectionsComplete();
		};

		BlendEdgeEdgeGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
		};

		BlendEdgeEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			if (!This->MetadataBlender) { return; }
			const TSharedRef<PCGExBlending::FMetadataBlender> Blender = This->MetadataBlender.ToSharedRef();

			TArray<PCGEx::FOpStats> Trackers;
			Blender->InitTrackers(Trackers);

			PCGEX_SCOPE_LOOP(Index) { This->EdgeEdgeIntersections->BlendIntersection(Index, Blender, Trackers); }
		};

		BlendEdgeEdgeGroup->StartSubLoops(EdgeEdgeIntersections->UniqueCrossings.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnEdgeEdgeIntersectionsComplete()
	{
		if (EdgeEdgeIntersections) { EdgeEdgeIntersections.Reset(); }
		UnionDataFacade->WriteFastest(Context->GetTaskManager());
	}

#pragma endregion

	void FUnionProcessor::CompileFinalGraph()
	{
		check(!bCompilingFinalGraph)

		bCompilingFinalGraph = true;

		Context->SetState(States::State_WritingClusters);
		GraphBuilder->OnCompilationEndCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TSharedRef<FGraphBuilder>& InBuilder, const bool bSuccess)
		{
			PCGEX_ASYNC_THIS
			if (!bSuccess) { This->UnionDataFacade->Source->InitializeOutput(PCGExData::EIOInit::NoInit); }
			else { This->GraphBuilder->StageEdgesOutputs(); }
		};

		// Make sure we provide up-to-date transform range to sort over
		GraphBuilder->NodePointsTransforms = GraphBuilder->NodeDataFacade->GetOut()->GetConstTransformValueRange();
		GraphBuilder->CompileAsync(Context->GetTaskManager(), true, &GraphMetadataDetails);
	}
}
