// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFloodFillClusters.h"


#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExClusterDiffusion"
#define PCGEX_NAMESPACE ClusterDiffusion

UPCGExClusterDiffusionSettings::UPCGExClusterDiffusionSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SeedForwarding.bPreservePCGExData = true;
}

#if WITH_EDITOR
void UPCGExClusterDiffusionSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 2)
	{
		// FillRate migrated from the Input/Attribute/Constant triple to FPCGExInputShorthandSelectorInteger32Abs.
		Diffusion.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExClusterDiffusionSettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExClusterDiffusionSettings::GetEdgeOutputInitMode() const
{
	return EdgeDirectionOutput.ResolveEdgeInitMode(WantsDataStealing());
}

TArray<FPCGPinProperties> UPCGExClusterDiffusionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "[DEPRECATED] Use 'Heuristics Scoring' fill control instead. Legacy heuristics input for backward compatibility.", Advanced, FPCGExDataTypeInfoHeuristics::AsId())
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points.", Required)
	PCGEX_PIN_FACTORIES(PCGExFloodFill::SourceFillControlsLabel, "Fill controls, used to constraint & limit flood fill", Normal, FPCGExDataTypeInfoFillControl::AsId())
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExClusterDiffusionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();

	if (PathOutput != EPCGExFloodFillPathOutput::None)
	{
		PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "High density, overlapping paths representing individual flood lanes", Normal)
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ClusterDiffusion)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ClusterDiffusion)

bool FPCGExClusterDiffusionElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDiffusion)
	PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_VALIDATE_NAME)

	Context->EdgeDirectionOutput = Settings->EdgeDirectionOutput;
	Context->EdgeDirectionOutput.ValidateNames(Context);

	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);

	// Fill controls are optional
	PCGExFactories::GetInputFactories<UPCGExFillControlsFactoryData>(Context, PCGExFloodFill::SourceFillControlsLabel, Context->FillControlFactories, {FPCGExDataTypeInfoFillControl::AsId()}, false);

	// Check for deprecated Heuristics pin usage
	if (Context->bHasValidHeuristics)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext,
		           FTEXT("The Heuristics pin on Cluster Diffusion is deprecated. "
			           "Use 'Fill Control : Heuristics Scoring' instead for more granular control. "
			           "The node-level Heuristics input will be removed in a future version."));
	}

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	if (Settings->PathOutput != EPCGExFloodFillPathOutput::None)
	{
		PCGEX_FWD(SeedAttributesToPathTags)
		if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
		{
			return false;
		}

		Context->Paths = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->Paths->OutputPin = PCGExPaths::Labels::OutputPathsLabel;
	}

	FPCGExForwardDetails FwdDetails = Settings->SeedForwarding;
	FwdDetails.bFilterToRemove = true;
	Context->SeedForwardHandler = FwdDetails.GetHandler(Context->SeedsDataFacade, false);

	return true;
}

bool FPCGExClusterDiffusionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClusterDiffusionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDiffusion)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();
	if (Context->Paths)
	{
		Context->Paths->StageOutputs();
	}

	return Context->TryComplete();
}

namespace PCGExClusterDiffusion
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::OnGrowthSetup()
	{
		EdgeDirectionDetails = Context->EdgeDirectionOutput;
		if (EdgeDirectionDetails.WantsDirection() && !EdgeDirectionDetails.InitForProcessor(Context, GetParentBatch<FBatch>()->EdgeDirectionOutput, EdgeDataFacade))
		{
			return false;
		}
		return true;
	}

	TSharedPtr<TArray<int8>> FProcessor::GetInfluencesCount() const
	{
		return InfluencesCount;
	}

	void FProcessor::CompleteWork()
	{
		if (Diffusions.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No valid diffusions."));
			bIsProcessorValid = false;
			return;
		}

		// Proceed to blending
		// Note: There is an important probability of collision for nodes with influences > 1

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, DiffuseDiffusions)

		DiffuseDiffusions->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnDiffusionComplete();
		};

		DiffuseDiffusions->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->Diffuse(This->Diffusions[Index]);
		};

		DiffuseDiffusions->StartIterations(Diffusions.Num(), 1);
	}

	void FProcessor::Diffuse(const TSharedPtr<PCGExFloodFill::FDiffusion>& Diffusion)
	{
		TArray<int32> Indices;

		// Diffuse & blend
		PCGExFloodFill::DiffuseAndBlend(*Diffusion, VtxDataFacade, BlendOpsManager, Indices);
		FPlatformAtomics::InterlockedAdd(&ExpectedPathCount, Diffusion->Endpoints.Num());
		FPlatformAtomics::InterlockedAdd(&Context->ExpectedPathCount, ExpectedPathCount);

		// Outputs
		if (!Indices.IsEmpty())
		{
			for (int i = 0; i < Indices.Num(); i++)
			{
				const PCGExFloodFill::FCandidate& Candidate = Diffusion->Captured[i];
				const int32 TargetIndex = Indices[i];

				PCGEX_OUTPUT_VALUE(DiffusionDepth, TargetIndex, Candidate.Depth);
				PCGEX_OUTPUT_VALUE(NormalizedDiffusionDepth, TargetIndex, Diffusion->GetMaxDepth() > 0 ? static_cast<double>(Candidate.Depth) / static_cast<double>(Diffusion->GetMaxDepth()) : 0.0);
				if (DiffusionDepths)
				{
					(*DiffusionDepths)[TargetIndex] = Candidate.Depth;
				}
				PCGEX_OUTPUT_VALUE(DiffusionDistance, TargetIndex, Candidate.PathDistance);
				PCGEX_OUTPUT_VALUE(DiffusionOrder, TargetIndex, i);
				PCGEX_OUTPUT_VALUE(DiffusionEnding, TargetIndex, Diffusion->Endpoints.Contains(Candidate.CaptureIndex));
			}

			// Forward seed values to diffusion
			if (Diffusion->SeedIndex != -1)
			{
				Context->SeedForwardHandler->Forward(Diffusion->SeedIndex, VtxDataFacade, Indices);
			}
		}

		// Diffusion->Captured.Empty(); // We need it for paths, TODO : turn diff data into shared vtx arrays on the batch instead.
		Diffusion->Candidates.Empty();

		// TODO : Cleanup the diffusion if we don't want paths
	}

	void FProcessor::OnDiffusionComplete()
	{
		if (Settings->PathOutput == EPCGExFloodFillPathOutput::None || ExpectedPathCount == 0)
		{
			return;
		}

		// Create the path writer for output
		PathWriter = MakeShared<PCGExFloodFill::FDiffusionPathWriter>(Cluster.ToSharedRef(), VtxDataFacade, Context->Paths.ToSharedRef(), TaskManager, DiffusionDepths);

		const FName NormPathDepthName = Settings->bWriteNormalizedPathDepth && Settings->PathOutput != EPCGExFloodFillPathOutput::None
			? Settings->NormalizedPathDepthAttributeName
			: NAME_None;
		const EPCGExFloodFillNormalizedPathDepthMode NormPathDepthMode = Settings->NormalizedPathDepthMode;

		if (Settings->PathOutput == EPCGExFloodFillPathOutput::Full)
		{
			// Output full path, rather straightforward
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, PathsTaskGroup)
			PathsTaskGroup->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, NormPathDepthName, NormPathDepthMode](const int32 Index, const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				TSharedPtr<PCGExFloodFill::FDiffusion> Diff = This->Diffusions[Index];
				for (const int32 EndpointIndex : Diff->Endpoints)
				{
					This->PathWriter->WriteFullPath(
						*Diff,
						Diff->Captured[EndpointIndex].Node->Index,
						Diff->Captured[EndpointIndex].Depth,
						Diff->GetMaxDepth(),
						NormPathDepthName,
						NormPathDepthMode,
						This->Context->SeedAttributesToPathTags,
						This->Context->SeedsDataFacade.ToSharedRef());
				}
			};

			PathsTaskGroup->StartIterations(Diffusions.Num(), 1);
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, PathsTaskGroup)
		PathsTaskGroup->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, SortOver = Settings->PathPartitions, SortOrder = Settings->PartitionSorting, NormPathDepthName, NormPathDepthMode](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			TSharedPtr<PCGExFloodFill::FDiffusion> Diff = This->Diffusions[Index];
			const TArray<PCGExFloodFill::FCandidate>& Captured = Diff->Captured;

			TSet<int32> Visited;
			Visited.Reserve(Captured.Num());

			TArray<int32> PathIndices;
			PathIndices.Reserve(Captured.Num());

			TArray<int32> Endpoints = Diff->Endpoints.Array();

			switch (SortOver)
			{
			case EPCGExFloodFillPathPartitions::Length:
				if (SortOrder == EPCGExSortDirection::Ascending)
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].PathDistance < Captured[B].PathDistance;
					});
				}
				else
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].PathDistance > Captured[B].PathDistance;
					});
				}
				break;
			case EPCGExFloodFillPathPartitions::Score:
				if (SortOrder == EPCGExSortDirection::Ascending)
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].PathScore < Captured[B].PathScore;
					});
				}
				else
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].PathScore > Captured[B].PathScore;
					});
				}
				break;
			case EPCGExFloodFillPathPartitions::Depth:
				if (SortOrder == EPCGExSortDirection::Ascending)
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].Depth < Captured[B].Depth;
					});
				}
				else
				{
					Endpoints.Sort([&](const int32 A, const int32 B)
					{
						return Captured[A].Depth > Captured[B].Depth;
					});
				}
				break;
			}

			// Cascade mode: per-diffusion array tracking hierarchical falloff values by vtx point index
			// 1.0 at seed, 0.0 at leaves, branches inherit parent value and lerp to 0
			TArray<double> CascadeValues;
			const bool bCascade = NormPathDepthMode == EPCGExFloodFillNormalizedPathDepthMode::Cascade && This->DiffusionDepths;
			if (bCascade)
			{
				CascadeValues.Init(-1.0, This->VtxDataFacade->GetNum());
			}

			for (const int32 EndpointIndex : Endpoints)
			{
				PathIndices.Reset();

				const int32 EndpointNodeIndex = Captured[EndpointIndex].Node->Index;
				const int32 EndpointDepth = Captured[EndpointIndex].Depth;

				int32 PathNodeIndex = PCGEx::NH64A(Diff->TravelStack->Get(EndpointNodeIndex));
				int32 PathEdgeIndex = -1;

				if (PathNodeIndex != -1)
				{
					int32 PathPointIndex = This->Cluster->GetNodePointIndex(EndpointNodeIndex);
					PathIndices.Add(PathPointIndex);
					Visited.Add(PathPointIndex);

					while (PathNodeIndex != -1)
					{
						const int32 CurrentIndex = PathNodeIndex;
						PCGEx::NH64(Diff->TravelStack->Get(CurrentIndex), PathNodeIndex, PathEdgeIndex);

						PathPointIndex = This->Cluster->GetNodePointIndex(CurrentIndex);
						PathIndices.Add(PathPointIndex);

						bool bIsAlreadyVisited = false;
						Visited.Add(PathPointIndex, &bIsAlreadyVisited);

						if (bIsAlreadyVisited)
						{
							PathNodeIndex = -1;
						}
					}
				}

				// Compute cascade values for this partition before writing
				// PathIndices are in endpoint(leaf)-first order here (not yet reversed)
				if (bCascade && PathIndices.Num() >= 2)
				{
					const TArray<int32>& Depths = *This->DiffusionDepths;

					// Last point in pre-reversal order = closest to seed (branch point or seed itself)
					const int32 BranchVtxIdx = PathIndices.Last();
					// First point in pre-reversal order = leaf endpoint
					const int32 LeafVtxIdx = PathIndices[0];

					const int32 BranchDepth = Depths[BranchVtxIdx];
					const int32 LeafDepth = Depths[LeafVtxIdx];
					const int32 DepthRange = LeafDepth - BranchDepth;

					// Branch point value: read from previous partition, or 1.0 if seed (first occurrence)
					const double BranchValue = CascadeValues[BranchVtxIdx] >= 0.0 ? CascadeValues[BranchVtxIdx] : 1.0;

					if (DepthRange > 0)
					{
						const double InvRange = 1.0 / static_cast<double>(DepthRange);
						for (int32 i = 0; i < PathIndices.Num(); i++)
						{
							const int32 VtxIdx = PathIndices[i];
							const double T = static_cast<double>(Depths[VtxIdx] - BranchDepth) * InvRange;
							CascadeValues[VtxIdx] = BranchValue * (1.0 - T);
						}
					}
					else
					{
						for (int32 i = 0; i < PathIndices.Num(); i++)
						{
							CascadeValues[PathIndices[i]] = BranchValue;
						}
					}
				}

				This->PathWriter->WritePartitionedPath(
					*Diff,
					PathIndices,
					EndpointDepth,
					Diff->GetMaxDepth(),
					NormPathDepthName,
					NormPathDepthMode,
					This->Context->SeedAttributesToPathTags,
					This->Context->SeedsDataFacade.ToSharedRef(),
					bCascade ? &CascadeValues : nullptr);
			}
		};

		PathsTaskGroup->StartIterations(Diffusions.Num(), 1);
	}

	void FProcessor::Write()
	{
		if (!EdgeDirectionDetails.IsActive())
		{
			return;
		}

		// Captured candidates persist until Cleanup(), so build the node-index -> depth lookup here
		// (rather than mirroring it during the parallel Diffuse pass) and resolve edge directions.
		TArray<int32> NodeDepths;
		NodeDepths.Init(-1, Cluster->Nodes->Num());
		for (const TSharedPtr<PCGExFloodFill::FDiffusion>& Diffusion : Diffusions)
		{
			for (const PCGExFloodFill::FCandidate& Candidate : Diffusion->Captured)
			{
				NodeDepths[Candidate.Node->Index] = Candidate.Depth;
			}
		}

		EdgeDirectionDetails.WriteFromNodeDepths(Cluster.Get(), NodeDepths);
		EdgeDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Cleanup()
	{
		// Base resets the growth state (diffusions + fill controls handler).
		TDiffusionGrowthProcessor<FPCGExClusterDiffusionContext, UPCGExClusterDiffusionSettings>::Cleanup();

		// Make sure we flush these ASAP
		BlendOpsManager.Reset();
		PathWriter.Reset();
	}


	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
		FPCGExClusterDiffusionContext* Ctx = static_cast<FPCGExClusterDiffusionContext*>(InContext);
		// Only request heuristics if the deprecated pin is connected
		// Modern approach is to use 'Heuristics Scoring' fill control instead
		SetWantsHeuristics(Ctx->GetHasValidHeuristics(), EPCGExHeuristicScoreMode::WeightedAverage);
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffusion)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;
			PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_INIT)
		}

		PCGExBlending::RegisterBuffersDependencies(Context, FacadePreloader, Context->BlendingFactories);

		for (const TObjectPtr<const UPCGExFillControlsFactoryData>& Factory : Context->FillControlFactories)
		{
			Factory->RegisterBuffersDependencies(Context, FacadePreloader);
			// TODO : Might need to fill-in facade here as well
		}

		EdgeDirectionOutput.RegisterBuffersDependencies(Context, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffusion)

		// Build the shared edge-direction sorter before the base triggers RegisterBuffersDependencies.
		EdgeDirectionOutput = Context->EdgeDirectionOutput;
		if (!EdgeDirectionOutput.InitForBatch(Context, VtxDataFacade, Context->GetEdgeSortingRules()))
		{
			bIsBatchValid = false;
			return;
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffusion)

		BlendOpsManager = MakeShared<PCGExBlending::FBlendOpsManager>(VtxDataFacade);
		if (!BlendOpsManager->Init(Context, Context->BlendingFactories))
		{
			bIsBatchValid = false;
			return;
		}

		InfluencesCount = MakeShared<TArray<int8>>();
		InfluencesCount->Init(0, VtxDataFacade->GetNum());

		// Lightweight depth array for NormalizedPathDepth (no buffer overhead)
		if (Settings->bWriteNormalizedPathDepth && Settings->PathOutput != EPCGExFloodFillPathOutput::None)
		{
			DiffusionDepths = MakeShared<TArray<int32>>();
			DiffusionDepths->Init(0, VtxDataFacade->GetNum());
		}

		// Diffusion rate

		FillRate = Settings->Diffusion.FillRate.GetValueSetting();
		bIsBatchValid = FillRate->Init(Settings->Diffusion.FillRateSource == EPCGExFloodFillSettingSource::Seed ? Context->SeedsDataFacade : VtxDataFacade);

		if (!bIsBatchValid)
		{
			return;
		} // Fail

		TBatch<FProcessor>::Process();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}

		PCGEX_TYPED_PROCESSOR

		TypedProcessor->BlendOpsManager = BlendOpsManager;
		TypedProcessor->InfluencesCount = InfluencesCount;
		TypedProcessor->DiffusionDepths = DiffusionDepths;

		TypedProcessor->FillRate = FillRate;

#define PCGEX_OUTPUT_FWD_TO(_NAME, _TYPE, _DEFAULT_VALUE) if(_NAME##Writer){ TypedProcessor->_NAME##Writer = _NAME##Writer; }
		PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_FWD_TO)
#undef PCGEX_OUTPUT_FWD_TO

		return true;
	}

	void FBatch::Write()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffusion)

		TBatch<FProcessor>::Write();
		BlendOpsManager->Cleanup(Context);
		VtxDataFacade->WriteFastest(TaskManager);
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
