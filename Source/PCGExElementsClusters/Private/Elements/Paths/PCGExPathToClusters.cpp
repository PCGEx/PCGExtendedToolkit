// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Paths/PCGExPathToClusters.h"


#include "Core/PCGExUnionData.h"
#include "Core/PCGExUnionRegistry.h"
#include "Core/PCGExUnionTable.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/Union/PCGExUnionProcessor.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPathToClustersElement"
#define PCGEX_NAMESPACE BuildCustomGraph

TArray<FPCGPinProperties> UPCGExPathToClustersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathToClusters)

TSharedPtr<PCGExPointsMT::IBatch> FPCGExPathToClustersContext::CreatePointBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const
{
	PCGEX_SETTINGS_LOCAL(PathToClusters)
	if (Settings->bFusePaths)
	{
		return MakeShared<PCGExPointsMT::TBatch<PCGExPathToClusters::FFusingProcessor>>(const_cast<FPCGExPathToClustersContext*>(this), InData);
	}
	return MakeShared<PCGExPointsMT::TBatch<PCGExPathToClusters::FNonFusingProcessor>>(const_cast<FPCGExPathToClustersContext*>(this), InData);
}

bool FPCGExPathToClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PathToClusters)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	const_cast<UPCGExPathToClustersSettings*>(Settings)->EdgeEdgeIntersectionDetails.Init();

	if (Settings->bFusePaths)
	{
		const TSharedPtr<PCGExData::FPointIO> UnionVtxPoints = PCGExData::NewPointIO(Context, Settings->GetMainOutputPin());
		UnionVtxPoints->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New);

		Context->UnionDataFacade = MakeShared<PCGExData::FFacade>(UnionVtxPoints.ToSharedRef());

		Context->FuseDetails = Settings->PointPointIntersectionDetails.FuseDetails;
		// TODO : Support local fuse distance, requires access to all input facades
		if (!Context->FuseDetails.Init(Context, nullptr))
		{
			return false;
		}

		Context->FuseBounds = Context->MainPoints->GetInBounds().ExpandBy(10);
		Context->bUseOctreeMode = (Context->FuseDetails.GetEffectiveMethod() == EPCGExFuseMethod::Octree);

		Context->NodeBuilder = MakeShared<PCGExData::FUnionTableBuilder>(1);
		Context->NodeBuilder->bDedupeElementsBySource = true; // node table: collapse shared-point duplicates
		Context->EdgeBuilder = MakeShared<PCGExData::FUnionTableBuilder>(1);
		if (Context->bUseOctreeMode)
		{
			Context->NodeRegistry = MakeShared<PCGExData::FUnionRegistry>(Context->FuseBounds);
		}

		Context->UnionProcessor = MakeShared<PCGExGraphs::FUnionProcessor>(Context, Context->UnionDataFacade.ToSharedRef(), Settings->PointPointIntersectionDetails, Settings->DefaultPointsBlendingDetails, Settings->DefaultEdgesBlendingDetails);

		Context->UnionProcessor->VtxCarryOverDetails = &Context->CarryOverDetails;

		if (Settings->bFindPointEdgeIntersections)
		{
			Context->UnionProcessor->InitPointEdge(Settings->PointEdgeIntersectionDetails, Settings->bUseCustomPointEdgeBlending, &Settings->CustomPointEdgeBlendingDetails);
		}

		if (Settings->bFindEdgeEdgeIntersections)
		{
			Context->UnionProcessor->InitEdgeEdge(Settings->EdgeEdgeIntersectionDetails, Settings->bUseCustomEdgeEdgeBlending, &Settings->CustomEdgeEdgeBlendingDetails);
		}
	}


	return true;
}

bool FPCGExPathToClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathToClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathToClusters)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (Settings->bFusePaths)
		{
			const bool bUseOctree = Context->bUseOctreeMode;
			PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					if (Entry->GetNum() < 2)
					{
						bHasInvalidInputs = true;
						return false;
					}
					return true;
				}, [bUseOctree](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
					NewBatch->bSkipCompletion = true;
					// Octree-fuse mode is sequential by registry contract; grid mode runs fully parallel.
					NewBatch->bForceSingleThreadedProcessing = bUseOctree;
				}))
			{
				return Context->CancelExecution(TEXT("Could not build any clusters."));
			}
		}
		else
		{
			PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					if (Entry->GetNum() < 2)
					{
						bHasInvalidInputs = true;
						return false;
					}
					return true;
				}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				return Context->CancelExecution(TEXT("Could not build any clusters."));
			}
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(Settings->bFusePaths ? PCGExGraphs::States::State_PreparingUnion : PCGExCommon::States::State_Done)

#pragma region Intersection management

	if (Settings->bFusePaths)
	{
		PCGEX_ON_STATE(PCGExGraphs::States::State_PreparingUnion)
		{
			using namespace PCGExPathToClusters;

			PCGExPointsMT::TBatch<FFusingProcessor>* MainBatch = static_cast<PCGExPointsMT::TBatch<FFusingProcessor>*>(Context->MainBatch.Get());

			const int32 NumFacades = MainBatch->ProcessorFacades.Num();
			Context->PathsFacades.Reserve(NumFacades);

			// Phase 1+2 collection. Same pattern as FuseClusters but the records are streamed in
			// processor (path) order. Edges are abstract (no per-edge source point) so the table
			// will only carry placeholder (IO=-1) entries -- skipped during blend, but counted in
			// UnionSize so the value matches legacy.
			TArray<PCGExData::FUnionStreamRecord>& NodeScope = Context->NodeBuilder->GetScope(0);
			TArray<FStagedEdge> AllStagedEdges;

			int32 EstNodeRecords = 0;
			int32 EstStagedEdges = 0;
			const int32 NumProcs = MainBatch->GetNumProcessors();
			for (int32 Pi = 0; Pi < NumProcs; Pi++)
			{
				const TSharedPtr<FFusingProcessor> P = MainBatch->GetProcessor<FFusingProcessor>(Pi);
				if (!P.IsValid() || !P->bIsProcessorValid)
				{
					continue;
				}
				EstNodeRecords += P->NodeRecords.Num();
				EstStagedEdges += P->StagedEdges.Num();
			}
			NodeScope.Reserve(EstNodeRecords);
			AllStagedEdges.Reserve(EstStagedEdges);

			for (int32 Pi = 0; Pi < NumProcs; Pi++)
			{
				const TSharedPtr<FFusingProcessor> P = MainBatch->GetProcessor<FFusingProcessor>(Pi);
				if (!P.IsValid() || !P->bIsProcessorValid)
				{
					continue;
				}
				Context->PathsFacades.Add(P->PointDataFacade);
				NodeScope.Append(MoveTemp(P->NodeRecords));
				AllStagedEdges.Append(MoveTemp(P->StagedEdges));
			}

			Context->MainBatch.Reset();

			// Phase 1 -- compile node table
			const TSharedPtr<PCGExData::FUnionTable> NodesTable = MakeShared<PCGExData::FUnionTable>();
			Context->NodeBuilder->Compile(*NodesTable);
			Context->NodeBuilder.Reset();

			const int32 NumUnionNodes = NodesTable->Num();
			if (NumUnionNodes == 0)
			{
				return Context->CancelExecution(TEXT("Union table is empty after fuse build."));
			}

			TMap<uint64, int32> KeyToNode;
			KeyToNode.Reserve(NumUnionNodes);
			for (int32 i = 0; i < NumUnionNodes; i++)
			{
				KeyToNode.Add(NodesTable->Keys[i], i);
			}

			// Phase 2 -- emit abstract edges. Placeholder (IO=-1, Index=0) so each unique edge has an
			// entry whose Size is the count of incoming paths that produced it; ComputeWeights skips
			// IO=-1 so the entry contributes nothing to blending, matching legacy abstract behavior.
			TArray<PCGExData::FUnionStreamRecord>& EdgeScope = Context->EdgeBuilder->GetScope(0);
			EdgeScope.Reserve(AllStagedEdges.Num());
			for (const FStagedEdge& Staged : AllStagedEdges)
			{
				const int32* NodeA = KeyToNode.Find(Staged.KeyA);
				const int32* NodeB = KeyToNode.Find(Staged.KeyB);
				if (!NodeA || !NodeB || *NodeA == *NodeB)
				{
					continue;
				}
				const uint64 EdgeKey = PCGEx::H64U(static_cast<uint32>(*NodeA), static_cast<uint32>(*NodeB));
				EdgeScope.Emplace(EdgeKey, -1, 0);
			}
			AllStagedEdges.Empty();

			const TSharedPtr<PCGExData::FUnionTable> EdgesTable = MakeShared<PCGExData::FUnionTable>();
			Context->EdgeBuilder->Compile(*EdgesTable);
			Context->EdgeBuilder.Reset();

			const int32 NumEdges = EdgesTable->Num();
			TArray<PCGExGraphs::FEdge> Edges;
			Edges.SetNumUninitialized(NumEdges);
			for (int32 i = 0; i < NumEdges; i++)
			{
				const uint64 K = EdgesTable->Keys[i];
				const int32 Start = static_cast<int32>(PCGEx::H64A(K));
				const int32 End = static_cast<int32>(PCGEx::H64B(K));
				Edges[i] = PCGExGraphs::FEdge(i, Start, End, -1, -1);
			}

			Context->UnionProcessor->SetUnionData(NodesTable, EdgesTable, MoveTemp(Edges), Context->FuseBounds);

			if (!Context->UnionProcessor->StartExecution(Context->PathsFacades, Settings->GraphBuilderDetails))
			{
				return Context->CancelExecution(TEXT("Could not start union."));
			}

			Context->NodeRegistry.Reset();
		}

		if (!Context->UnionProcessor->Execute())
		{
			return false;
		}

		Context->Done();
	}

#pragma endregion

	if (Settings->bFusePaths)
	{
		(void)Context->UnionDataFacade->Source->StageOutput(Context);
	}
	else
	{
		Context->MainPoints->StageOutputs();
	}

	return Context->TryComplete();
}

namespace PCGExPathToClusters
{
#pragma region NonFusing

	FNonFusingProcessor::~FNonFusingProcessor()
	{
	}

	bool FNonFusingProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointIO->GetIn());

		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

		const int32 NumPoints = PointDataFacade->GetNum();

		PointIO->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New);

		TArray<PCGExGraphs::FEdge> Edges;
		PCGExArrayHelpers::InitArray(Edges, bClosedLoop ? NumPoints : NumPoints - 1);

		for (int i = 0; i < Edges.Num(); i++)
		{
			Edges[i] = PCGExGraphs::FEdge(i, i, i + 1, PointIO->IOIndex);
		}

		if (bClosedLoop)
		{
			const int32 LastIndex = Edges.Num() - 1;
			Edges[LastIndex] = PCGExGraphs::FEdge(LastIndex, LastIndex, 0, PointIO->IOIndex);
		}

		GraphBuilder->Graph->InsertEdges(Edges);
		Edges.Empty();

		GraphBuilder->CompileAsync(TaskManager, false);

		return true;
	}

	void FNonFusingProcessor::CompleteWork()
	{
		if (!GraphBuilder->bCompiledSuccessfully)
		{
			bIsProcessorValid = false;
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
			return;
		}

		GraphBuilder->StageEdgesOutputs();
		PointDataFacade->WriteFastest(TaskManager);
	}

#pragma endregion

#pragma region Fusing

	FFusingProcessor::~FFusingProcessor()
	{
	}

	bool FFusingProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		IOIndex = PointDataFacade->Source->IOIndex;
		LastIndex = NumPoints - 1;

		if (NumPoints < 2)
		{
			return false;
		}

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn());

		// Each point contributes 1 node record (and 2 records for the closing edge in a closed loop).
		// Each edge between successive points contributes 1 staged edge + endpoint duplicates handled
		// during sort-and-group. Reserve generously to avoid TArray growth in the hot loop.
		const int32 NumEdges = bClosedLoop ? NumPoints : NumPoints - 1;
		NodeRecords.Reserve(NumEdges * 2);
		StagedEdges.Reserve(NumEdges);

		EmitEdges(PCGExMT::FScope(0, NumPoints));

		return true;
	}

	void FFusingProcessor::EmitEdges(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathToClusters::FFusingProcessor::EmitEdges);

		const FPCGExFuseDetails& FuseDetails = Context->FuseDetails;
		const bool bUseOctree = Context->bUseOctreeMode;
		const TSharedPtr<PCGExData::FUnionRegistry> Registry = Context->NodeRegistry;

		auto KeyOf = [&](const PCGExData::FConstPoint& Pt) -> uint64
		{
			if (bUseOctree)
			{
				return static_cast<uint64>(Registry->FindOrInsert(Pt, FuseDetails));
			}
			return FuseDetails.GetGridKey(Pt.GetLocation(), Pt.Index);
		};

		auto EmitPathEdge = [&](const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To)
		{
			const uint64 KeyA = KeyOf(From);
			const uint64 KeyB = KeyOf(To);
			NodeRecords.Emplace(KeyA, From.IO, From.Index);
			NodeRecords.Emplace(KeyB, To.IO, To.Index);
			StagedEdges.Emplace(KeyA, KeyB);
		};

		PCGEX_SCOPE_LOOP(i)
		{
			const int32 NextIndex = i + 1;
			if (NextIndex > LastIndex)
			{
				if (bClosedLoop)
				{
					EmitPathEdge(PointDataFacade->GetInPoint(LastIndex), PointDataFacade->GetInPoint(0));
				}
				return;
			}
			EmitPathEdge(PointDataFacade->GetInPoint(i), PointDataFacade->GetInPoint(NextIndex));
		}
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
