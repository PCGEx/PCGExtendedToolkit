// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBFSDepth.h"

#include "Data/PCGExData.h"
#include "Clusters/PCGExCluster.h"

#define LOCTEXT_NAMESPACE "PCGExBFSDepth"
#define PCGEX_NAMESPACE BFSDepth

#pragma region UPCGExBFSDepthSettings

PCGExData::EIOInit UPCGExBFSDepthSettings::GetMainOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }
PCGExData::EIOInit UPCGExBFSDepthSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::Forward; }

TArray<FPCGPinProperties> UPCGExBFSDepthSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points used as BFS starting positions.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BFSDepth)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(BFSDepth)

#pragma endregion

#pragma region FPCGExBFSDepthElement

bool FPCGExBFSDepthElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BFSDepth)
	PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_VALIDATE_NAME)

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade) { return false; }

	return true;
}

bool FPCGExBFSDepthElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBFSDepthElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BFSDepth)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExBFSDepth::FProcessor

namespace PCGExBFSDepth
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBFSDepth::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		if (Context->SeedsDataFacade->GetNum() <= 0) { return false; }
		
		Depths.Init(-1, NumNodes);
		Seeded.Init(0, NumNodes);

		if (Settings->bUseOctreeSearch) { Cluster->RebuildOctree(Settings->SeedPicking.PickingMethod); }

		
		PCGEX_ASYNC_GROUP_CHKD(TaskManager, SeedPickingGroup)

		SeedPickingGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->RunBFS();
		};

		SeedPickingGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->SeedNodeIndices = MakeShared<PCGExMT::TScopedArray<FIntPoint>>(Loops);
		};

		SeedPickingGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			TConstPCGValueRange<FTransform> SeedTransforms = This->Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			const TArray<PCGExClusters::FNode>& Nodes = *This->Cluster->Nodes.Get();

			PCGEX_SCOPE_LOOP(Index)
			{
				const FVector SeedLocation = SeedTransforms[Index].GetLocation();
				const int32 ClosestIndex = This->Cluster->FindClosestNode(SeedLocation, This->Settings->SeedPicking.PickingMethod);

				if (ClosestIndex < 0) { continue; }

				const PCGExClusters::FNode* SeedNode = &Nodes[ClosestIndex];
				if (!This->Settings->SeedPicking.WithinDistance(This->Cluster->GetPos(SeedNode), SeedLocation) ||
					FPlatformAtomics::InterlockedCompareExchange(&This->Seeded[ClosestIndex], 1, 0) == 1)
				{
					continue;
				}

				This->SeedNodeIndices->Get(Scope)->Add(FIntPoint(ClosestIndex, Index));
			}
		};

		

		SeedPickingGroup->StartSubLoops(Context->SeedsDataFacade->GetNum(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);

		return true;
	}

	void FProcessor::RunBFS()
	{
		SeedNodeIndices->Collapse(CollectedSeeds);
		SeedNodeIndices.Reset();

		if (CollectedSeeds.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A cluster could not match any seed points. Check seed positions and picking distance."));
			bIsProcessorValid = false;
			return;
		}

		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		const bool bComputeDistance = DistanceData != nullptr;
		const bool bTrackSeedOwner = SeedIndexData != nullptr;

		if (bComputeDistance) { Distances.Init(-1.0, Nodes.Num()); }
		if (bTrackSeedOwner) { SeedOwners.Init(-1, Nodes.Num()); }

		// Initialize all seeds at depth 0
		TArray<int32> Queue;
		Queue.Reserve(Nodes.Num());

		for (const FIntPoint& Seed : CollectedSeeds)
		{
			const int32 NodeIdx = Seed.X;
			const int32 SeedPtIdx = Seed.Y;
			const int32 PointIdx = Nodes[NodeIdx].PointIndex;

			Depths[NodeIdx] = 0;
			if (DepthData) { DepthData[PointIdx] = 0; }

			if (bComputeDistance)
			{
				Distances[NodeIdx] = 0.0;
				DistanceData[PointIdx] = 0.0;
			}

			if (bTrackSeedOwner)
			{
				SeedOwners[NodeIdx] = SeedPtIdx;
				SeedIndexData[PointIdx] = SeedPtIdx;
			}

			Queue.Add(NodeIdx);
		}

		// BFS — branched to avoid sqrt when distance output is disabled
		int32 Head = 0;

		if (bComputeDistance)
		{
			while (Head < Queue.Num())
			{
				const int32 CurrentIdx = Queue[Head++];
				const PCGExClusters::FNode& Current = Nodes[CurrentIdx];
				const FVector CurrentPos = Cluster->GetPos(CurrentIdx);
				const int32 NextDepth = Depths[CurrentIdx] + 1;
				const double CurrentDist = Distances[CurrentIdx];

				for (const PCGExGraphs::FLink& Lk : Current.Links)
				{
					if (Depths[Lk.Node] != -1) { continue; }

					const int32 NeighborPointIdx = Nodes[Lk.Node].PointIndex;
					const double NewDist = CurrentDist + FVector::Distance(CurrentPos, Cluster->GetPos(Lk.Node));

					Depths[Lk.Node] = NextDepth;
					Distances[Lk.Node] = NewDist;

					if (DepthData) { DepthData[NeighborPointIdx] = NextDepth; }
					DistanceData[NeighborPointIdx] = NewDist;
					if (bTrackSeedOwner) { SeedOwners[Lk.Node] = SeedOwners[CurrentIdx]; SeedIndexData[NeighborPointIdx] = SeedOwners[CurrentIdx]; }

					Queue.Add(Lk.Node);
				}
			}
		}
		else
		{
			while (Head < Queue.Num())
			{
				const int32 CurrentIdx = Queue[Head++];
				const PCGExClusters::FNode& Current = Nodes[CurrentIdx];
				const int32 NextDepth = Depths[CurrentIdx] + 1;

				for (const PCGExGraphs::FLink& Lk : Current.Links)
				{
					if (Depths[Lk.Node] != -1) { continue; }

					Depths[Lk.Node] = NextDepth;
					if (DepthData) { DepthData[Nodes[Lk.Node].PointIndex] = NextDepth; }
					if (bTrackSeedOwner) { SeedOwners[Lk.Node] = SeedOwners[CurrentIdx]; SeedIndexData[Nodes[Lk.Node].PointIndex] = SeedOwners[CurrentIdx]; }

					Queue.Add(Lk.Node);
				}
			}
		}
	}

#pragma endregion

#pragma region PCGExBFSDepth::FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(BFSDepth)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;
			PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_INIT)
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }

		PCGEX_TYPED_PROCESSOR

		if (DepthWriter) { TypedProcessor->DepthData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(DepthWriter)->GetOutValues()->GetData(); }
		if (DistanceWriter) { TypedProcessor->DistanceData = StaticCastSharedPtr<PCGExData::TArrayBuffer<double>>(DistanceWriter)->GetOutValues()->GetData(); }
		if (SeedIndexWriter) { TypedProcessor->SeedIndexData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(SeedIndexWriter)->GetOutValues()->GetData(); }

		return true;
	}

	void FBatch::Write()
	{
		VtxDataFacade->WriteFastest(TaskManager);
		TBatch<FProcessor>::Write();
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
