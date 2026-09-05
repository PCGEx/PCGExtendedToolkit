// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/Union/PCGExUnionProcessor.h"


#include "PCGExCoreSettingsCache.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExEdge.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExProxyDataBlending.h"
#include "Core/PCGExUnionTable.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/Union/PCGExIntersections.h"

namespace PCGExGraphs
{
	FUnionProcessor::FUnionProcessor(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade> InUnionDataFacade, FPCGExPointPointIntersectionDetails InPointPointIntersectionSettings, FPCGExBlendingDetails InDefaultPointsBlending, FPCGExBlendingDetails InDefaultEdgesBlending)
		: Context(InContext)
		  , UnionDataFacade(InUnionDataFacade)
		  , PointPointIntersectionDetails(InPointPointIntersectionSettings)
		  , DefaultPointsBlendingDetails(InDefaultPointsBlending)
		  , DefaultEdgesBlendingDetails(InDefaultEdgesBlending)
	{
	}

	void FUnionProcessor::SetUnionData(
		const TSharedPtr<PCGExData::FUnionTable>& InNodesTable,
		const TSharedPtr<PCGExData::FUnionTable>& InEdgesTable,
		TArray<FEdge>&& InEdges,
		const FBox& InBounds)
	{
		NodesTable = InNodesTable;
		EdgesTable = InEdgesTable;
		Edges = MoveTemp(InEdges);
		Bounds = InBounds;
	}

	FUnionProcessor::~FUnionProcessor()
	{
	}

	void FUnionProcessor::InitPointEdge(const FPCGExPointEdgeIntersectionDetails& InDetails, const bool bUseCustom, const FPCGExBlendingDetails* InOverride)
	{
		bDoPointEdge = true;
		PointEdgeIntersectionDetails = InDetails;
		bUseCustomPointEdgeBlending = bUseCustom;
		if (InOverride)
		{
			CustomPointEdgeBlendingDetails = *InOverride;
		}
	}

	void FUnionProcessor::InitEdgeEdge(const FPCGExEdgeEdgeIntersectionDetails& InDetails, const bool bUseCustom, const FPCGExBlendingDetails* InOverride)
	{
		bDoEdgeEdge = true;
		EdgeEdgeIntersectionDetails = InDetails;
		EdgeEdgeIntersectionDetails.Init();
		bUseCustomEdgeEdgeBlending = bUseCustom;
		if (InOverride)
		{
			CustomEdgeEdgeBlendingDetails = *InOverride;
		}
	}

	bool FUnionProcessor::StartExecution(const TArray<TSharedRef<PCGExData::FFacade>>& InFacades, const FPCGExGraphBuilderDetails& InBuilderDetails)
	{
		BuilderDetails = InBuilderDetails;

		check(NodesTable && EdgesTable);

		const int32 NumUnionNodes = NodesTable->Num();
		if (NumUnionNodes == 0)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Union graph is empty. Something is likely corrupted."));
			return false;
		}

		Context->SetState(States::State_ProcessingUnion);

		const TSharedPtr<PCGExBlending::FUnionBlender> TypedBlender = MakeShared<PCGExBlending::FUnionBlender>(
			&DefaultPointsBlendingDetails, VtxCarryOverDetails, PointPointIntersectionDetails.FuseDetails.GetDistances());

		UnionBlender = TypedBlender;

		TypedBlender->AddSources(InFacades, &PCGExClusters::Labels::ProtectedClusterAttributes);

		UPCGBasePointData* MutablePoints = UnionDataFacade->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumUnionNodes, UnionBlender->GetAllocatedProperties()); // TODO : Proper Allocation

		if (!TypedBlender->Init(Context, UnionDataFacade, NodesTable))
		{
			return false;
		}

		// New natural ordering: adopt the graph state up front so downstream phases (P/E, E/E, finalize)
		// see a fully wired FGraph. Only dependency is that MutablePoints is already sized -- which it is.
		GraphMetadataDetails.Update(Context, PointPointIntersectionDetails);
		GraphMetadataDetails.Update(Context, PointEdgeIntersectionDetails);
		GraphMetadataDetails.Update(Context, EdgeEdgeIntersectionDetails);
		GraphMetadataDetails.EdgesBlendingDetailsPtr = bUseCustomEdgeEdgeBlending ? &CustomEdgeEdgeBlendingDetails : &DefaultEdgesBlendingDetails;
		GraphMetadataDetails.EdgesCarryOverDetails = EdgesCarryOverDetails;

		GraphBuilder = MakeShared<FGraphBuilder>(UnionDataFacade, &BuilderDetails);
		GraphBuilder->bInheritNodeData = false;
		GraphBuilder->bRequiresEdgeResort = false; // All insertion is sequential → deterministic node ordering
		// TODO(perf): expose an opt-in Morton remap step on FUnionTableBuilder so we can reinstate the
		// fast-path. Lexicographic key sort is deterministic but not Morton-spatial.
		GraphBuilder->bNodesPreSorted = false;
		GraphBuilder->SourceEdgeFacades = SourceEdgesIO;
		GraphBuilder->Graph->NodesUnion = NodesTable;
		GraphBuilder->Graph->EdgesUnion = EdgesTable;

		// Initialize per-node UnionSize from table sizes in one explicit pass. Per-edge UnionSize
		// is seeded via the FGraph at AdoptEdges time.
		for (int32 i = 0; i < NumUnionNodes; i++)
		{
			GraphBuilder->Graph->GetOrCreateNodeMetadata_Unsafe(i).UnionSize = NodesTable->Size(i);
		}

		GraphBuilder->Graph->AdoptEdges(Edges);

		const int32 NumEdges = EdgesTable->Num();
		for (int32 i = 0; i < NumEdges; i++)
		{
			GraphBuilder->Graph->GetOrCreateEdgeMetadata_Unsafe(i).UnionSize = EdgesTable->Size(i);
		}

		PCGEX_ASYNC_GROUP_CHKD(Context->GetTaskManager(), ProcessNodesGroup)

		ProcessNodesGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionBlender.Reset();
			This->OnNodesProcessingComplete();
		};

		ProcessNodesGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE, InFacades](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			const TSharedPtr<PCGExBlending::IUnionBlender> Blender = This->UnionBlender;
			const TSharedPtr<PCGExData::FUnionTable> Table = This->NodesTable;

			TArray<PCGExData::FWeightedPoint> WeightedPoints;
			TArray<PCGEx::FOpStats> Trackers;
			Blender->InitTrackers(Trackers);

			UPCGBasePointData* OutPoints = This->UnionDataFacade->GetOut();
			TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange(false);

			PCGEX_SCOPE_LOOP(Index)
			{
				const TConstArrayView<PCGExData::FElement> Span = Table->Get(Index);

				// Centroid of the source point locations.
				FVector Center = FVector::ZeroVector;
				int32 ContributingPoints = 0;
				for (const PCGExData::FElement& E : Span)
				{
					if (E.IO < 0 || !InFacades.IsValidIndex(E.IO))
					{
						continue;
					}
					Center += InFacades[E.IO]->GetIn()->GetTransform(E.Index).GetLocation();
					ContributingPoints++;
				}
				if (ContributingPoints > 0)
				{
					Center /= static_cast<double>(ContributingPoints);
				}

				OutTransforms[Index].SetLocation(Center);
				Blender->MergeSingle(Index, Span, WeightedPoints, Trackers);
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

		// Adoption happened in StartExecution. Only thing left here is flushing blended attribute
		// buffers before P/E reads them. Critical: we collect the per-buffer write callbacks first
		// and *only* spin up an async group if there's actual work -- creating one without queued
		// tasks registers an orphan token that hangs the graph forever.
		TArray<PCGExMT::FSimpleCallback> WriteCallbacks = UnionDataFacade->GetWriteBufferCallbacks();
		if (WriteCallbacks.IsEmpty())
		{
			UnionDataFacade->Flush();
			InternalStartExecution();
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), WriteMetadataTask);
		WriteMetadataTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->UnionDataFacade->Flush();
			This->InternalStartExecution();
		};

		WriteMetadataTask->AddSimpleCallbacks(MoveTemp(WriteCallbacks));
		WriteMetadataTask->StartSimpleCallbacks();
	}

	void FUnionProcessor::InternalStartExecution()
	{
		if (GraphBuilder->Graph->Edges.Num() <= 1)
		{
			CompileFinalGraph();
		} // Nothing to be found
		else if (bDoPointEdge)
		{
			FindPointEdgeIntersections();
		}
		else if (bDoEdgeEdge)
		{
			FindEdgeEdgeIntersections();
		}
		else
		{
			CompileFinalGraph();
		}
	}

	bool FUnionProcessor::Execute()
	{
		if (!bRunning || Context->IsState(States::State_ProcessingUnion))
		{
			return false;
		}

		PCGEX_ON_ASYNC_STATE_READY(States::State_ProcessingPointEdgeIntersections)
		{
			if (bDoEdgeEdge)
			{
				FindEdgeEdgeIntersections();
			}
			else
			{
				CompileFinalGraph();
			}
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

		Context->SetState(States::State_ProcessingPointEdgeIntersections);

		// Fresh allocations -- the graph has been adopted but not yet mutated by P/E. We rebuild
		// rather than try to share with E/E since E/E runs against a post-P/E graph (different
		// edge count) and uses its own tolerance.
		IntersectionAllocations = MakeShared<FIntersectionAllocations>(GraphBuilder->Graph, UnionDataFacade->Source);
		IntersectionAllocations->Build(PointEdgeIntersectionDetails.FuseDetails.Tolerance);
		if (!PointEdgeIntersectionDetails.bEnableSelfIntersection)
		{
			IntersectionAllocations->BuildRootIOSets();
		}

		// Init point octree (P/E queries the *output* point octree, so it has to exist)
		(void)IntersectionAllocations->PointIO->GetOutIn()->GetPointOctree();

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), FindPointEdgeGroup)

		FindPointEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnPointEdgeIntersectionsFound();
		};

		FindPointEdgeGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->ScopedPERecords = MakeShared<PCGExMT::TScopedArray<FPECollinear>>(Loops);
			// Pre-reserve scope arrays at ~1 record per edge in the scope. Avoids the realloc
			// cascade as records accumulate; over-allocation is cheap (records are small PODs).
			for (const PCGExMT::FScope& S : Loops)
			{
				This->ScopedPERecords->Get_Ref(S).Reserve(S.Count);
			}
		};

		FindPointEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			TArray<FPECollinear>& Records = This->ScopedPERecords->Get_Ref(Scope);
			PointEdgePass::Emit(*This->IntersectionAllocations, This->PointEdgeIntersectionDetails, This->PointEdgeIntersectionDetails.bEnableSelfIntersection, Scope, Records);
		};

		FindPointEdgeGroup->StartSubLoops(GraphBuilder->Graph->Edges.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnPointEdgeIntersectionsFound()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::OnPointEdgeIntersectionsFound);

		if (!ScopedPERecords)
		{
			OnPointEdgeIntersectionsComplete();
			return;
		}

		// Drain scope-local records into a single flat array. Order across scopes is stable since
		// TScopedArray::Collapse concatenates in scope-index order; the apply pass sorts anyway.
		TArray<FPECollinear> Records;
		ScopedPERecords->Collapse(Records);
		ScopedPERecords.Reset();

		PENum = Records.Num();

		if (Records.IsEmpty())
		{
			OnPointEdgeIntersectionsComplete();
			return;
		}

		PointEdgePass::Apply(*IntersectionAllocations, PointEdgeIntersectionDetails, Records);

		// Blend pass -- still a no-op body (proper P/E lerp still TODO). We retain
		// the async group + blender plumbing so it's a one-line edit when the time comes.
		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), BlendPointEdgeGroup)

		UnionDataFacade->Source->ClearCachedKeys();

		MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
		MetadataBlender->SetTargetData(UnionDataFacade);
		MetadataBlender->SetSourceData(UnionDataFacade, PCGExData::EIOSide::Out);

		if (!MetadataBlender->Init(Context, bUseCustomPointEdgeBlending ? CustomPointEdgeBlendingDetails : DefaultPointsBlendingDetails, &PCGExClusters::Labels::ProtectedClusterAttributes))
		{
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
			if (!This->MetadataBlender)
			{
				return;
			}
			const TSharedRef<PCGExBlending::FMetadataBlender> Blender = This->MetadataBlender.ToSharedRef();

			PCGEX_SCOPE_LOOP(Index)
			{
				// TODO : per-record blend via PointEdgePass::BlendIntersection
			}
		};

		BlendPointEdgeGroup->StartSubLoops(Records.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnPointEdgeIntersectionsComplete()
	{
		// Allocations and scoped records are scratch -- release before E/E rebuilds them.
		IntersectionAllocations.Reset();
		if (MetadataBlender)
		{
			UnionDataFacade->WriteFastest(Context->GetTaskManager());
		}
	}

#pragma endregion

#pragma region EdgeEdge

	void FUnionProcessor::FindEdgeEdgeIntersections()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::FindEdgeEdgeIntersections);

		Context->SetState(States::State_ProcessingEdgeEdgeIntersections);

		IntersectionAllocations = MakeShared<FIntersectionAllocations>(GraphBuilder->Graph, UnionDataFacade->Source);
		IntersectionAllocations->Build(EdgeEdgeIntersectionDetails.Tolerance);
		if (!EdgeEdgeIntersectionDetails.bEnableSelfIntersection)
		{
			IntersectionAllocations->BuildRootIOSets();
		}
		IntersectionAllocations->BuildEdgeOctree(Bounds);

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), FindEdgeEdgeGroup)

		FindEdgeEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnEdgeEdgeIntersectionsFound();
		};

		FindEdgeEdgeGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->ScopedEERecords = MakeShared<PCGExMT::TScopedArray<FEECrossing>>(Loops);
			for (const PCGExMT::FScope& S : Loops)
			{
				This->ScopedEERecords->Get_Ref(S).Reserve(S.Count);
			}
		};

		FindEdgeEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			TArray<FEECrossing>& Records = This->ScopedEERecords->Get_Ref(Scope);
			EdgeEdgePass::Emit(*This->IntersectionAllocations, This->EdgeEdgeIntersectionDetails, This->EdgeEdgeIntersectionDetails.bEnableSelfIntersection, Scope, Records);
		};

		FindEdgeEdgeGroup->StartSubLoops(GraphBuilder->Graph->Edges.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnEdgeEdgeIntersectionsFound()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionProcessor::OnEdgeEdgeIntersectionsFound);

		if (!ScopedEERecords)
		{
			OnEdgeEdgeIntersectionsComplete();
			return;
		}

		ScopedEERecords->Collapse(EECrossings);
		ScopedEERecords.Reset();

		EENum = EECrossings.Num();

		if (EECrossings.IsEmpty())
		{
			OnEdgeEdgeIntersectionsComplete();
			return;
		}

		// Phase 2 (sequential): dedupe + endpoint-reuse + node allocation.
		const int32 NewlyAllocated = EdgeEdgePass::ResolveCrossings(*IntersectionAllocations, EECrossings);

		// Phase 3 (sequential): subdivide edges.
		EdgeEdgePass::Apply(*IntersectionAllocations, EECrossings);

		UnionDataFacade->Source->ClearCachedKeys();

		// Phase 4 (parallel): blend at each newly-allocated crossing node. Skip if no new nodes
		// (i.e. every crossing resolved onto an existing endpoint -- no new node needs blending).
		if (NewlyAllocated <= 0)
		{
			OnEdgeEdgeIntersectionsComplete();
			return;
		}

		MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
		MetadataBlender->SetTargetData(UnionDataFacade);
		MetadataBlender->SetSourceData(UnionDataFacade, PCGExData::EIOSide::Out);

		if (!MetadataBlender->Init(Context, bUseCustomEdgeEdgeBlending ? CustomEdgeEdgeBlendingDetails : DefaultPointsBlendingDetails, &PCGExClusters::Labels::ProtectedClusterAttributes))
		{
			Context->CancelExecution(FString("Error initializing Edge/Edge blending"));
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(Context->GetTaskManager(), BlendEdgeEdgeGroup)

		BlendEdgeEdgeGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnEdgeEdgeIntersectionsComplete();
		};

		BlendEdgeEdgeGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			if (!This->MetadataBlender)
			{
				return;
			}
			const TSharedRef<PCGExBlending::FMetadataBlender> Blender = This->MetadataBlender.ToSharedRef();

			TArray<PCGEx::FOpStats> Trackers;
			Blender->InitTrackers(Trackers);

			PCGEX_SCOPE_LOOP(Index)
			{
				const FEECrossing& Crossing = This->EECrossings[Index];
				// Only primaries that allocated a new node need blending. Duplicates and reused-
				// endpoint records share the existing node's already-blended state.
				if (!Crossing.bIsPrimary || !Crossing.bAllocatedNewNode)
				{
					continue;
				}
				EdgeEdgePass::BlendIntersection(*This->IntersectionAllocations, Blender, Crossing, Trackers);
			}
		};

		BlendEdgeEdgeGroup->StartSubLoops(EECrossings.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize * 2);
	}

	void FUnionProcessor::OnEdgeEdgeIntersectionsComplete()
	{
		EECrossings.Empty();
		IntersectionAllocations.Reset();
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
			if (!bSuccess)
			{
				This->UnionDataFacade->Source->InitializeOutput(PCGExData::EIOInit::NoInit);
			}
			else
			{
				This->GraphBuilder->StageEdgesOutputs();
			}
		};

		// Make sure we provide up-to-date transform range to sort over
		GraphBuilder->NodePointsTransforms = GraphBuilder->NodeDataFacade->GetOut()->GetConstTransformValueRange();
		GraphBuilder->CompileAsync(Context->GetTaskManager(), true, &GraphMetadataDetails);
	}
}
