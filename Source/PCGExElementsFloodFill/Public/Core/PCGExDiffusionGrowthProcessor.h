// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "PCGExCoreSettingsCache.h"

namespace PCGExFloodFill
{
	/**
	 * Shared growth engine for diffusion-style cluster processors.
	 *
	 * Owns seed picking, diffusion initialization and the parallel/sequential growth loop --
	 * everything up to (but not including) the per-diffusion output pass. Concrete processors
	 * supply their output in CompleteWork() and customize two seams:
	 *   - OnGrowthSetup()      : node-specific Process()-time setup (return false to abort).
	 *   - GetInfluencesCount() : the shared per-vtx claim array, or null to disable claiming
	 *                            (overlapping diffusions that don't coordinate captures).
	 *
	 * Dependent-base members are reached through `this->` (two-phase lookup); the async-callback
	 * bodies reach them through the pinned `This` handle, exactly as in a concrete processor.
	 *
	 * Instantiating contexts must expose `SeedsDataFacade` and `FillControlFactories`; settings
	 * must expose `Seeds`, `Processing`, `Diffusion` and `bUseOctreeSearch`.
	 */
	template <typename TContext, typename TSettings>
	class TDiffusionGrowthProcessor : public PCGExClusterMT::TProcessor<TContext, TSettings>
	{
	protected:
		// PCGEX_ASYNC_THIS_* expand to an unqualified SharedThis(this); in a template derived from a
		// dependent base, two-phase lookup won't find the inherited TSharedFromThis member, so we
		// reintroduce it explicitly. (Concrete processors don't need this -- their base is non-dependent.)
		using PCGExClusterMT::IProcessor::SharedThis;

		// Deterministic seed-node claims: per node, the LOWEST seed index that resolved to it (MAX_int32 = unclaimed).
		TArray<int32> Seeded;
		// Per seed, the node it resolved to (-1 when out of range / no node). Consumed by StartGrowth.
		TArray<int32> SeedClosestNode;

		TSharedPtr<FFillControlsHandler> FillControlsHandler;

		TArray<TSharedPtr<FDiffusion>> OngoingDiffusions;
		TArray<TSharedPtr<FDiffusion>> Diffusions;        // Stopped diffusions, as to not iterate over them needlessly

		// When true, ProcessRange exhausts each diffusion instead of stepping it by FillRate. Set by the
		// growth driver when diffusions cannot interact (no claiming, no shared capture state) or when a
		// single diffusion remains (no competitor left to interleave with).
		bool bGrowthRunToCompletion = false;

		/** Node-specific Process()-time setup, run before diffusion initialization. Return false to abort. */
		virtual bool OnGrowthSetup() { return true; }

		/** Per-vtx claim array shared across diffusions. Null disables claiming (overlapping diffusions). */
		virtual TSharedPtr<TArray<int8>> GetInfluencesCount() const { return nullptr; }

		/** Whether the node itself consumes FDiffusion::TravelStack (e.g. path output). Controls declare their own need. */
		virtual bool NeedsTravelStack() const { return false; }

		/** Per-diffusion growth rate; <= 0 means the seed vtx is preserved from diffusing. */
		FORCEINLINE int32 ReadFillRate(const TSharedPtr<FDiffusion>& Diffusion) const
		{
			return FillRate->Read(Diffusion->GetSettingsIndex(this->Settings->Diffusion.FillRateSource));
		}

	public:
		// Set by the owning batch; drives how many growth steps each diffusion takes per iteration.
		TSharedPtr<PCGExDetails::TSettingValue<int32>> FillRate;

		TDiffusionGrowthProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: PCGExClusterMT::TProcessor<TContext, TSettings>(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFloodFill::DiffusionGrowth::Process);

			if (!PCGExClusterMT::IProcessor::Process(InTaskManager))
			{
				return false;
			}

			if (!OnGrowthSetup())
			{
				return false;
			}

			const int32 NumSeeds = this->Context->SeedsDataFacade->GetNum();
			if (NumSeeds <= 0)
			{
				return false;
			}

			FillControlsHandler = MakeShared<FFillControlsHandler>(this->Context, this->Cluster, this->VtxDataFacade, this->EdgeDataFacade, this->Context->SeedsDataFacade, this->Context->FillControlFactories);

			FillControlsHandler->HeuristicsHandler = this->HeuristicsHandler;
			FillControlsHandler->InfluencesCount = GetInfluencesCount();
			FillControlsHandler->bNeedsTravelStack |= NeedsTravelStack();

			Seeded.Init(MAX_int32, this->Cluster->Nodes->Num());
			SeedClosestNode.Init(-1, NumSeeds);

			if (this->Settings->bUseOctreeSearch)
			{
				this->Cluster->RebuildOctree(this->Settings->Seeds.SeedPicking.PickingMethod);
			}

			PCGEX_ASYNC_GROUP_CHKD(this->TaskManager, DiffusionInitialization)
			DiffusionInitialization->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->StartGrowth();
			};

			DiffusionInitialization->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS

				TConstPCGValueRange<FTransform> SeedTransforms = This->Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();

				PCGEX_SCOPE_LOOP(Index)
				{
					FVector SeedLocation = SeedTransforms[Index].GetLocation();
					const int32 ClosestIndex = This->Cluster->FindClosestNode(SeedLocation, This->Settings->Seeds.SeedPicking.PickingMethod);

					if (ClosestIndex < 0 || !This->Settings->Seeds.SeedPicking.WithinDistance(This->Cluster->GetPos(ClosestIndex), SeedLocation))
					{
						continue;
					}

					This->SeedClosestNode[Index] = ClosestIndex;

					// Contested nodes go to the LOWEST seed index -- atomic min keeps the outcome
					// deterministic regardless of thread scheduling.
					int32 Current = This->Seeded[ClosestIndex];
					while (Index < Current)
					{
						const int32 Prev = FPlatformAtomics::InterlockedCompareExchange(&This->Seeded[ClosestIndex], Index, Current);
						if (Prev == Current)
						{
							break;
						}
						Current = Prev;
					}
				}
			};

			DiffusionInitialization->StartSubLoops(NumSeeds, PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);

			return true;
		}

		void StartGrowth()
		{
			// Materialize diffusions for winning seeds, in ascending seed order (deterministic).
			const int32 NumSeeds = SeedClosestNode.Num();
			const TArray<PCGExClusters::FNode>& Nodes = *this->Cluster->Nodes.Get();

			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; SeedIdx++)
			{
				const int32 NodeIdx = SeedClosestNode[SeedIdx];
				if (NodeIdx < 0 || Seeded[NodeIdx] != SeedIdx)
				{
					// No node in range, or the node went to a lower seed index
					continue;
				}

				TSharedPtr<FDiffusion> NewDiffusion = MakeShared<FDiffusion>(FillControlsHandler, this->Cluster, &Nodes[NodeIdx]);
				NewDiffusion->Index = OngoingDiffusions.Num();
				NewDiffusion->SeedIndex = SeedIdx;
				OngoingDiffusions.Add(NewDiffusion);
			}

			Seeded.Empty();
			SeedClosestNode.Empty();

			if (OngoingDiffusions.IsEmpty())
			{
				PCGE_LOG_C(Warning, GraphAndLog, this->Context, FTEXT("A cluster could not initialize any diffusions. This is usually caused when there is more clusters than there is seeds, or all available seeds were better candidates for other clusters."));
				this->bIsProcessorValid = false;
				return;
			}

			// Prepare control handler before initializing diffusions since the init does a first probing pass.
			// This is also what makes the final diffusion count (and thus per-diffusion reserves) available to Init.
			if (!FillControlsHandler->PrepareForDiffusions(OngoingDiffusions, this->Settings->Diffusion))
			{
				PCGE_LOG_C(Warning, GraphAndLog, this->Context, FTEXT("Fill controls handler failed to prepare for diffusions. Check that all fill control inputs are valid."));
				this->bIsProcessorValid = false;
				return;
			}

			// Init only touches per-diffusion state (plus each diffusion's unique slot in the shared claim array),
			// so diffusions initialize concurrently -- allocation & the first probe are the heavy parts.
			PCGExMT::ParallelOrSequential(
				OngoingDiffusions.Num(),
				[&](const int32 i)
				{
					OngoingDiffusions[i]->Init();
				},
				2, EParallelForFlags::Unbalanced);

			Diffusions.Reserve(OngoingDiffusions.Num());

			Grow();
		}

		void Grow()
		{
			// Iterative drivers only: growth used to re-enter itself (Grow -> round -> completion -> Grow),
			// stacking 3+ frames per round -- a stack overflow on large clusters at low fill rates.

			if (this->Settings->Processing != EPCGExFloodFillProcessing::Parallel)
			{
				// Sequential: exhaust one diffusion at a time, in seed order.
				for (const TSharedPtr<FDiffusion>& Diffusion : OngoingDiffusions)
				{
					if (ReadFillRate(Diffusion) <= 0)
					{
						// Zero rate preserves the seed vtx from diffusing
						Diffusion->bStopped = true;
					}
					while (!Diffusion->bStopped)
					{
						Diffusion->Grow();
					}
					Diffusions.Add(Diffusion);
				}
				OngoingDiffusions.Reset();
				return;
			}

			// Parallel: without claiming or shared capture state, diffusions cannot interact -- lockstep
			// interleaving buys nothing, so each diffusion runs to completion in a single pass.
			bGrowthRunToCompletion = !FillControlsHandler->InfluencesCount && !FillControlsHandler->bHasCaptureNotify;

			while (!OngoingDiffusions.IsEmpty())
			{
				// StartParallelLoopForRange bails without running compaction when the work handle dies;
				// bail with it or this loop never terminates.
				if (!this->WorkHandle.IsValid())
				{
					return;
				}

				// A single competitor left has nothing to interleave with -- exhaust it.
				if (OngoingDiffusions.Num() == 1)
				{
					bGrowthRunToCompletion = true;
				}

				// One synchronous pass; OnRangeProcessingComplete compacts OngoingDiffusions.
				// Run-to-completion passes use single-iteration chunks for work stealing across uneven diffusions.
				this->StartParallelLoopForRange(OngoingDiffusions.Num(), bGrowthRunToCompletion ? 1 : -1);
			}
		}

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				const TSharedPtr<FDiffusion> Diffusion = OngoingDiffusions[Index];

				const int32 CurrentFillRate = ReadFillRate(Diffusion);
				if (CurrentFillRate <= 0)
				{
					// Zero rate preserves the seed vtx from diffusing. Park the diffusion for good --
					// leaving it ongoing would spin the growth loop forever.
					Diffusion->bStopped = true;
					continue;
				}

				if (bGrowthRunToCompletion)
				{
					while (!Diffusion->bStopped)
					{
						Diffusion->Grow();
					}
					continue;
				}

				for (int i = 0; i < CurrentFillRate; i++)
				{
					Diffusion->Grow();
				}
			}
		}

		virtual void OnRangeProcessingComplete() override
		{
			// A single growth pass is complete: move stopped diffusions in another castle.
			// Compaction only -- the Grow() driver loop owns re-entry.
			const int32 OngoingNum = OngoingDiffusions.Num();

			int32 WriteIndex = 0;
			for (int32 i = 0; i < OngoingNum; i++)
			{
				const TSharedPtr<FDiffusion> Diff = OngoingDiffusions[i];
				if (Diff->bStopped)
				{
					Diffusions.Add(Diff);
				}
				else
				{
					OngoingDiffusions[WriteIndex++] = Diff;
				}
			}

			OngoingDiffusions.SetNum(WriteIndex);
		}

		virtual void Cleanup() override
		{
			PCGExClusterMT::TProcessor<TContext, TSettings>::Cleanup();

			// Make sure we flush these ASAP
			Seeded.Empty();
			SeedClosestNode.Empty();
			OngoingDiffusions.Reset();
			Diffusions.Reset();
			FillControlsHandler.Reset();
		}
	};
}
