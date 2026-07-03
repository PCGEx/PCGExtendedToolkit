// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBFSDepth.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Factories/PCGExFactories.h"

#define LOCTEXT_NAMESPACE "PCGExBFSDepth"
#define PCGEX_NAMESPACE BFSDepth

#pragma region UPCGExBFSDepthSettings

PCGExData::EIOInit UPCGExBFSDepthSettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExBFSDepthSettings::GetEdgeOutputInitMode() const
{
	return EdgeDirectionOutput.ResolveEdgeInitMode(WantsDataStealing());
}

TArray<FPCGPinProperties> UPCGExBFSDepthSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points used as BFS starting positions.", Required)
	if (TriggerCount.bOutputTriggerCount)
	{
		PCGEX_PIN_FILTERS(PCGExBFSDepth::Labels::SourceTriggerFiltersLabel, "Vtx filters defining which vtx are 'triggers' that increment the trigger count.", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BFSDepth)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(BFSDepth)

#pragma endregion

#pragma region FPCGExBFSDepthElement

bool FPCGExBFSDepthElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BFSDepth)
	PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_VALIDATE_NAME)

	Context->EdgeDirectionOutput = Settings->EdgeDirectionOutput;
	Context->EdgeDirectionOutput.ValidateNames(Context);

	Context->TriggerCount = Settings->TriggerCount;
	Context->TriggerCount.ValidateNames(Context);

	if (Settings->TriggerCount.bOutputTriggerCount)
	{
		// Required: trigger count is meaningless without filters defining what a trigger is.
		if (!PCGExFactories::GetInputFactories(Context, PCGExBFSDepth::Labels::SourceTriggerFiltersLabel, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters()))
		{
			return false;
		}
	}

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	// Handlers are built per-batch (writers must be pre-created on each batch's vtx facade); only the
	// adjusted details live on the context.
	Context->SeedForwardDetails = Settings->SeedForwarding;
	Context->SeedForwardDetails.bFilterToRemove = true;

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
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
				if (!Context->VtxFilterFactories.IsEmpty())
				{
					NewBatch->VtxFilterFactories = &Context->VtxFilterFactories;
				}
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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Context->SeedsDataFacade->GetNum() <= 0)
		{
			return false;
		}

		EdgeDirectionDetails = Context->EdgeDirectionOutput;
		if (EdgeDirectionDetails.WantsDirection() && !EdgeDirectionDetails.InitForProcessor(Context, GetParentBatch<FBatch>()->EdgeDirectionOutput, EdgeDataFacade))
		{
			return false;
		}

		Depths.Init(-1, NumNodes);
		Seeded.Init(0, NumNodes);

		if (TriggerCountPtr && TriggerCountPtr->IsActive())
		{
			TriggerCounts.Init(0, NumNodes);
		}

		if (NormalizedDepthData)
		{
			Parents.Init(-1, NumNodes);
			ChildCount.Init(0, NumNodes);

			// Init sentinel for cascade: -1.0 = unset
			for (int32 i = 0; i < VtxDataFacade->GetNum(); i++)
			{
				NormalizedDepthData[i] = -1.0;
			}
		}

		if (Settings->bUseOctreeSearch)
		{
			Cluster->RebuildOctree(Settings->SeedPicking.PickingMethod);
		}


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

				if (ClosestIndex < 0)
				{
					continue;
				}

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
		// Seed forwarding needs per-vtx ownership even when the SeedIndex output is off.
		const bool bTrackSeedOwner = SeedIndexData != nullptr || SeedForwardHandler != nullptr;
		const bool bTrackParents = !Parents.IsEmpty();
		const bool bWriteTriggers = TriggerCountPtr && TriggerCountPtr->IsActive();
		const bool bTriggerCountsSelf = bWriteTriggers && TriggerCountPtr->CountsTriggerItself();

		// Resolve every vtx's trigger flag up-front (parallel) into the shared filter cache so the
		// per-node lookups below are O(1) array reads via IsNodePassingFilters().
		if (bWriteTriggers)
		{
			FilterVtxScope(PCGExMT::FScope(0, NumNodes, 0), true);
		}

		if (bComputeDistance)
		{
			Distances.Init(-1.0, Nodes.Num());
		}
		if (bTrackSeedOwner)
		{
			SeedOwners.Init(-1, Nodes.Num());
		}

		// Initialize all seeds at depth 0
		TArray<int32> Queue;
		Queue.Reserve(Nodes.Num());

		for (const FIntPoint& Seed : CollectedSeeds)
		{
			const int32 NodeIdx = Seed.X;
			const int32 SeedPtIdx = Seed.Y;
			const int32 PointIdx = Nodes[NodeIdx].PointIndex;

			Depths[NodeIdx] = 0;
			if (DepthData)
			{
				DepthData[PointIdx] = 0;
			}

			if (bComputeDistance)
			{
				Distances[NodeIdx] = 0.0;
				DistanceData[PointIdx] = 0.0;
			}

			if (bTrackSeedOwner)
			{
				SeedOwners[NodeIdx] = SeedPtIdx;
				if (SeedIndexData)
				{
					SeedIndexData[PointIdx] = SeedPtIdx;
				}
			}

			if (bWriteTriggers)
			{
				const int32 SeedFlag = bTriggerCountsSelf && IsNodePassingFilters(Nodes[NodeIdx]) ? 1 : 0;
				const int32 SeedValue = TriggerCountPtr->GetInitialValue(SeedPtIdx) + SeedFlag;
				TriggerCounts[NodeIdx] = SeedValue;
				TriggerCountPtr->Set(PointIdx, SeedValue);
			}

			Queue.Add(NodeIdx);
		}

		// BFS -- branched to avoid sqrt when distance output is disabled
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
					if (Depths[Lk.Node] != -1)
					{
						continue;
					}

					const int32 NeighborPointIdx = Nodes[Lk.Node].PointIndex;
					const double NewDist = CurrentDist + FVector::Distance(CurrentPos, Cluster->GetPos(Lk.Node));

					Depths[Lk.Node] = NextDepth;
					MaxBFSDepth = FMath::Max(MaxBFSDepth, NextDepth);
					Distances[Lk.Node] = NewDist;
					if (bTrackParents)
					{
						Parents[Lk.Node] = CurrentIdx;
						ChildCount[CurrentIdx]++;
					}

					if (DepthData)
					{
						DepthData[NeighborPointIdx] = NextDepth;
					}
					DistanceData[NeighborPointIdx] = NewDist;
					if (bTrackSeedOwner)
					{
						SeedOwners[Lk.Node] = SeedOwners[CurrentIdx];
						if (SeedIndexData)
						{
							SeedIndexData[NeighborPointIdx] = SeedOwners[CurrentIdx];
						}
					}

					if (bWriteTriggers)
					{
						const int32 Flag = (bTriggerCountsSelf ? IsNodePassingFilters(Nodes[Lk.Node]) : IsNodePassingFilters(Nodes[CurrentIdx])) ? 1 : 0;
						const int32 ChildValue = TriggerCounts[CurrentIdx] + Flag;
						TriggerCounts[Lk.Node] = ChildValue;
						TriggerCountPtr->Set(NeighborPointIdx, ChildValue);
					}

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
					if (Depths[Lk.Node] != -1)
					{
						continue;
					}

					Depths[Lk.Node] = NextDepth;
					MaxBFSDepth = FMath::Max(MaxBFSDepth, NextDepth);
					if (bTrackParents)
					{
						Parents[Lk.Node] = CurrentIdx;
						ChildCount[CurrentIdx]++;
					}
					if (DepthData)
					{
						DepthData[Nodes[Lk.Node].PointIndex] = NextDepth;
					}
					if (bTrackSeedOwner)
					{
						SeedOwners[Lk.Node] = SeedOwners[CurrentIdx];
						if (SeedIndexData)
						{
							SeedIndexData[Nodes[Lk.Node].PointIndex] = SeedOwners[CurrentIdx];
						}
					}

					if (bWriteTriggers)
					{
						const int32 Flag = (bTriggerCountsSelf ? IsNodePassingFilters(Nodes[Lk.Node]) : IsNodePassingFilters(Nodes[CurrentIdx])) ? 1 : 0;
						const int32 ChildValue = TriggerCounts[CurrentIdx] + Flag;
						TriggerCounts[Lk.Node] = ChildValue;
						TriggerCountPtr->Set(Nodes[Lk.Node].PointIndex, ChildValue);
					}

					Queue.Add(Lk.Node);
				}
			}
		}

		if (NormalizedDepthData)
		{
			ComputeNormalizedDepth();
		}

		// Cross/back edges aren't seen during discovery, so write every visited-visited edge in one pass.
		if (EdgeDirectionDetails.IsActive())
		{
			EdgeDirectionDetails.WriteFromNodeDepths(Cluster.Get(), Depths);
		}

		// Forward seed attributes onto the vtx each seed reached: bucket reached vtx by owning seed,
		// then fan each seed's row out in one prepared pass (writers were pre-created at batch scope).
		if (SeedForwardHandler)
		{
			TMap<int32, TArray<int32>> ForwardBuckets;
			ForwardBuckets.Reserve(CollectedSeeds.Num());
			for (int32 i = 0; i < Nodes.Num(); i++)
			{
				if (SeedOwners[i] >= 0)
				{
					ForwardBuckets.FindOrAdd(SeedOwners[i]).Add(Nodes[i].PointIndex);
				}
			}
			for (const TPair<int32, TArray<int32>>& Bucket : ForwardBuckets)
			{
				SeedForwardHandler->Forward(Bucket.Key, Bucket.Value);
			}
		}
	}

	void FProcessor::ComputeNormalizedDepth()
	{
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		const int32 TotalNodes = Nodes.Num();

		if (MaxBFSDepth <= 0)
		{
			return;
		}

		if (Settings->NormalizedDepthMode == EPCGExBFSNormalizedDepthMode::Global)
		{
			// Simple depth / MaxDepth -- parallelizable
			const double InvMax = 1.0 / static_cast<double>(MaxBFSDepth);
			PCGEX_PARALLEL_FOR(
				TotalNodes,
				if (Depths[i] >= 0) { NormalizedDepthData[Nodes[i].PointIndex] = static_cast<double>(Depths[i]) * InvMax; }
				);
			return;
		}

		// Cascade mode: hierarchical falloff through BFS tree
		// 1.0 at seeds, 0.0 at leaves, branches inherit and decay
		// NormalizedDepthData was pre-initialized to -1.0 as sentinel for "unset"

		// Collect leaf nodes (no BFS children, but reachable)
		TArray<int32> Leaves;
		Leaves.Reserve(TotalNodes / 4);
		for (int32 i = 0; i < TotalNodes; i++)
		{
			if (Depths[i] >= 0 && ChildCount[i] == 0)
			{
				Leaves.Add(i);
			}
		}

		// Sort by depth descending (longest branches first for priority)
		Leaves.Sort([this](const int32 A, const int32 B)
		{
			return Depths[A] > Depths[B];
		});

		// Reusable path buffer -- avoids per-leaf allocation
		TArray<int32> Path;
		Path.Reserve(MaxBFSDepth + 1);

		for (const int32 LeafIdx : Leaves)
		{
			Path.Reset();

			// Trace from leaf back to seed via parent pointers
			int32 Current = LeafIdx;
			while (Current != -1)
			{
				Path.Add(Current);
				Current = Parents[Current];
			}
			// Path[0]=leaf, Path.Last()=seed

			if (Path.Num() < 2)
			{
				continue;
			}

			// Find the branch point: walk from seed end, first node already set
			double BranchValue = 1.0;
			int32 BranchDepth = 0;
			for (int32 i = Path.Num() - 1; i >= 0; i--)
			{
				const double Existing = NormalizedDepthData[Nodes[Path[i]].PointIndex];
				if (Existing >= 0.0)
				{
					BranchValue = Existing;
					BranchDepth = Depths[Path[i]];
					break;
				}
			}

			const int32 DepthRange = Depths[LeafIdx] - BranchDepth;

			if (DepthRange > 0)
			{
				const double InvRange = 1.0 / static_cast<double>(DepthRange);
				for (const int32 NodeIdx : Path)
				{
					const int32 PtIdx = Nodes[NodeIdx].PointIndex;
					if (NormalizedDepthData[PtIdx] >= 0.0)
					{
						continue;
					} // Already set by a longer branch
					const double T = static_cast<double>(Depths[NodeIdx] - BranchDepth) * InvRange;
					NormalizedDepthData[PtIdx] = BranchValue * (1.0 - T);
				}
			}
			else
			{
				for (const int32 NodeIdx : Path)
				{
					const int32 PtIdx = Nodes[NodeIdx].PointIndex;
					if (NormalizedDepthData[PtIdx] < 0.0)
					{
						NormalizedDepthData[PtIdx] = BranchValue;
					}
				}
			}
		}

		// Clamp remaining -1.0 sentinels to 0.0 (unreachable nodes)
		PCGEX_PARALLEL_FOR(
			VtxDataFacade->GetNum(),
			if (NormalizedDepthData[i] < 0.0) { NormalizedDepthData[i] = 0.0; }
			);
	}

	void FProcessor::CompleteWork()
	{
		// Edge directions are written synchronously in RunBFS, so the facade is ready to flush here.
		if (EdgeDirectionDetails.IsActive()) { EdgeDataFacade->WriteFastest(TaskManager); }
	}

#pragma endregion

#pragma region PCGExBFSDepth::FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		EdgeDirectionOutput.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(BFSDepth)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;
			PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_INIT)

			if (Settings->bWriteNormalizedDepth)
			{
				NormalizedDepthWriter = OutputFacade->GetWritable<double>(Settings->NormalizedDepthAttributeName, 0.0, true, PCGExData::EBufferInit::New);
			}
		}

		// Trigger count: writer on the shared vtx facade, initial-value reader on the seeds facade.
		TriggerCount = Context->TriggerCount;
		if (!TriggerCount.InitForBatch(Context, VtxDataFacade, Context->SeedsDataFacade.ToSharedRef()))
		{
			bIsBatchValid = false;
			return;
		}

		// Prepared forward handler: readers on the seeds facade, writers on this batch's vtx facade,
		// all created here (single-threaded) so the per-processor BFS passes never create buffers.
		SeedForwardHandler = Context->SeedForwardDetails.TryGetHandler(Context->SeedsDataFacade, VtxDataFacade, false);

		// Build the shared edge-direction sorter before the base triggers RegisterBuffersDependencies.
		EdgeDirectionOutput = Context->EdgeDirectionOutput;
		if (!EdgeDirectionOutput.InitForBatch(Context, VtxDataFacade, Context->GetEdgeSortingRules()))
		{
			bIsBatchValid = false;
			return;
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}

		PCGEX_TYPED_PROCESSOR

		if (DepthWriter)
		{
			TypedProcessor->DepthData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(DepthWriter)->GetOutValues()->GetData();
		}
		if (DistanceWriter)
		{
			TypedProcessor->DistanceData = StaticCastSharedPtr<PCGExData::TArrayBuffer<double>>(DistanceWriter)->GetOutValues()->GetData();
		}
		if (SeedIndexWriter)
		{
			TypedProcessor->SeedIndexData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(SeedIndexWriter)->GetOutValues()->GetData();
		}
		if (NormalizedDepthWriter)
		{
			TypedProcessor->NormalizedDepthData = StaticCastSharedPtr<PCGExData::TArrayBuffer<double>>(NormalizedDepthWriter)->GetOutValues()->GetData();
		}

		TypedProcessor->TriggerCountPtr = &TriggerCount;
		TypedProcessor->SeedForwardHandler = SeedForwardHandler;

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
