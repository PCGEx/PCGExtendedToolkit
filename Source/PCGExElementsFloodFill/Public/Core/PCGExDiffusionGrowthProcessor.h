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

		TArray<int8> Seeded;
		TSharedPtr<FFillControlsHandler> FillControlsHandler;

		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<FDiffusion>>> InitialDiffusions;
		TArray<TSharedPtr<FDiffusion>> OngoingDiffusions;
		TArray<TSharedPtr<FDiffusion>> Diffusions;        // Stopped diffusions, as to not iterate over them needlessly

		/** Node-specific Process()-time setup, run before diffusion initialization. Return false to abort. */
		virtual bool OnGrowthSetup() { return true; }

		/** Per-vtx claim array shared across diffusions. Null disables claiming (overlapping diffusions). */
		virtual TSharedPtr<TArray<int8>> GetInfluencesCount() const { return nullptr; }

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

			FillControlsHandler = MakeShared<FFillControlsHandler>(this->Context, this->Cluster, this->VtxDataFacade, this->EdgeDataFacade, this->Context->SeedsDataFacade, this->Context->FillControlFactories);

			FillControlsHandler->HeuristicsHandler = this->HeuristicsHandler;
			FillControlsHandler->InfluencesCount = GetInfluencesCount();

			Seeded.Init(0, this->Cluster->Nodes->Num());

			PCGEX_ASYNC_GROUP_CHKD(this->TaskManager, DiffusionInitialization)
			DiffusionInitialization->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->StartGrowth();
			};

			DiffusionInitialization->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
			{
				PCGEX_ASYNC_THIS
				This->InitialDiffusions = MakeShared<PCGExMT::TScopedArray<TSharedPtr<FDiffusion>>>(Loops);
			};

			if (this->Settings->bUseOctreeSearch)
			{
				this->Cluster->RebuildOctree(this->Settings->Seeds.SeedPicking.PickingMethod);
			}

			DiffusionInitialization->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS

				const TArray<PCGExClusters::FNode>& Nodes = *This->Cluster->Nodes.Get();
				TConstPCGValueRange<FTransform> SeedTransforms = This->Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();

				PCGEX_SCOPE_LOOP(Index)
				{
					FVector SeedLocation = SeedTransforms[Index].GetLocation();
					const int32 ClosestIndex = This->Cluster->FindClosestNode(SeedLocation, This->Settings->Seeds.SeedPicking.PickingMethod);

					if (ClosestIndex < 0)
					{
						continue;
					}

					const PCGExClusters::FNode* SeedNode = &Nodes[ClosestIndex];
					if (!This->Settings->Seeds.SeedPicking.WithinDistance(This->Cluster->GetPos(SeedNode), SeedLocation) || FPlatformAtomics::InterlockedCompareExchange(&This->Seeded[ClosestIndex], 1, 0) == 1)
					{
						continue;
					}

					TSharedPtr<FDiffusion> NewDiffusion = MakeShared<FDiffusion>(This->FillControlsHandler, This->Cluster, SeedNode);
					NewDiffusion->Index = Index;
					This->InitialDiffusions->Get(Scope)->Add(NewDiffusion);
				}
			};

			if (this->Context->SeedsDataFacade->GetNum() <= 0)
			{
				return false;
			}

			DiffusionInitialization->StartSubLoops(this->Context->SeedsDataFacade->GetNum(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);

			return true;
		}

		void StartGrowth()
		{
			Seeded.Empty();

			InitialDiffusions->Collapse(OngoingDiffusions);
			InitialDiffusions.Reset();

			if (OngoingDiffusions.IsEmpty())
			{
				PCGE_LOG_C(Warning, GraphAndLog, this->Context, FTEXT("A cluster could not initialize any diffusions. This is usually caused when there is more clusters than there is seeds, or all available seeds were better candidates for other clusters."));
				this->bIsProcessorValid = false;
				return;
			}

			// Prepare control handler before initializing diffusion
			// since the init does a first probing pass
			if (!FillControlsHandler->PrepareForDiffusions(OngoingDiffusions, this->Settings->Diffusion))
			{
				PCGE_LOG_C(Warning, GraphAndLog, this->Context, FTEXT("Fill controls handler failed to prepare for diffusions. Check that all fill control inputs are valid."));
				this->bIsProcessorValid = false;
				return;
			}

			for (int i = 0; i < OngoingDiffusions.Num(); i++)
			{
				TSharedPtr<FDiffusion> Diffusion = OngoingDiffusions[i];
				const int32 InitIndex = Diffusion->Index;
				Diffusion->Index = i;
				Diffusion->Init(InitIndex);
			}

			Diffusions.Reserve(OngoingDiffusions.Num());

			Grow();
		}

		void Grow()
		{
			if (OngoingDiffusions.IsEmpty())
			{
				return;
			}

			if (this->Settings->Processing == EPCGExFloodFillProcessing::Parallel)
			{
				// Grow all by a single step
				this->StartParallelLoopForRange(OngoingDiffusions.Num());
				return;
			}

			// Grow one entirely
			TSharedPtr<FDiffusion> Diffusion = OngoingDiffusions.Pop();
			while (!Diffusion->bStopped)
			{
				Diffusion->Grow();
			}

			Diffusions.Add(Diffusion);

			Grow(); // Move to the next
		}

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				const TSharedPtr<FDiffusion> Diffusion = OngoingDiffusions[Index];
				const int32 CurrentFillRate = FillRate->Read(Diffusion->GetSettingsIndex(this->Settings->Diffusion.FillRateSource));
				for (int i = 0; i < CurrentFillRate; i++)
				{
					Diffusion->Grow();
				}
			}
		}

		virtual void OnRangeProcessingComplete() override
		{
			// A single growth iteration pass is complete
			const int32 OngoingNum = OngoingDiffusions.Num();

			// Move stopped diffusions in another castle
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

			if (OngoingDiffusions.IsEmpty())
			{
				return;
			}

			Grow();
		}

		virtual void Cleanup() override
		{
			PCGExClusterMT::TProcessor<TContext, TSettings>::Cleanup();

			// Make sure we flush these ASAP
			InitialDiffusions.Reset();
			OngoingDiffusions.Reset();
			Diffusions.Reset();
			FillControlsHandler.Reset();
		}
	};
}
