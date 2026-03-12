// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFloodFillClusters.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExClusterDiffusion"
#define PCGEX_NAMESPACE ClusterDiffusion

UPCGExClusterDiffusionSettings::UPCGExClusterDiffusionSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SeedForwarding.bPreservePCGExData = true;
}

PCGExData::EIOInit UPCGExClusterDiffusionSettings::GetMainOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }
PCGExData::EIOInit UPCGExClusterDiffusionSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::Forward; }

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
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDiffusion)
	PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_VALIDATE_NAME)
	
	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {PCGExFactories::EType::Blending}, false);

	// Fill controls are optional
	PCGExFactories::GetInputFactories<UPCGExFillControlsFactoryData>(Context, PCGExFloodFill::SourceFillControlsLabel, Context->FillControlFactories, {PCGExFactories::EType::FillControls}, false);

	// Check for deprecated Heuristics pin usage
	if (Context->bHasValidHeuristics)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext,
		           FTEXT("The Heuristics pin on Cluster Diffusion is deprecated. "
			           "Use 'Fill Control : Heuristics Scoring' instead for more granular control. "
			           "The node-level Heuristics input will be removed in a future version."));
	}

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade) { return false; }

	if (Settings->PathOutput != EPCGExFloodFillPathOutput::None)
	{
		PCGEX_FWD(SeedAttributesToPathTags)
		if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade)) { return false; }

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
		if (!Context->StartProcessingClusters([](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		{
			NewBatch->bRequiresWriteStep = true;
		}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();
	if (Context->Paths) { Context->Paths->StageOutputs(); }

	return Context->TryComplete();
}

namespace PCGExClusterDiffusion
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExClusterDiffusion::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		FillControlsHandler = MakeShared<PCGExFloodFill::FFillControlsHandler>(Context, Cluster, VtxDataFacade, EdgeDataFacade, Context->SeedsDataFacade, Context->FillControlFactories);

		FillControlsHandler->HeuristicsHandler = HeuristicsHandler;
		FillControlsHandler->InfluencesCount = InfluencesCount;

		Seeded.Init(0, Cluster->Nodes->Num());

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, DiffusionInitialization)
		DiffusionInitialization->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->StartGrowth();
		};

		DiffusionInitialization->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->InitialDiffusions = MakeShared<PCGExMT::TScopedArray<TSharedPtr<PCGExFloodFill::FDiffusion>>>(Loops);
		};

		if (Settings->bUseOctreeSearch) { Cluster->RebuildOctree(Settings->Seeds.SeedPicking.PickingMethod); }

		DiffusionInitialization->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			const TArray<PCGExClusters::FNode>& Nodes = *This->Cluster->Nodes.Get();
			TConstPCGValueRange<FTransform> SeedTransforms = This->Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();

			PCGEX_SCOPE_LOOP(Index)
			{
				FVector SeedLocation = SeedTransforms[Index].GetLocation();
				const int32 ClosestIndex = This->Cluster->FindClosestNode(SeedLocation, This->Settings->Seeds.SeedPicking.PickingMethod);

				if (ClosestIndex < 0) { continue; }

				const PCGExClusters::FNode* SeedNode = &Nodes[ClosestIndex];
				if (!This->Settings->Seeds.SeedPicking.WithinDistance(This->Cluster->GetPos(SeedNode), SeedLocation) || FPlatformAtomics::InterlockedCompareExchange(&This->Seeded[ClosestIndex], 1, 0) == 1) { continue; }

				TSharedPtr<PCGExFloodFill::FDiffusion> NewDiffusion = MakeShared<PCGExFloodFill::FDiffusion>(This->FillControlsHandler, This->Cluster, SeedNode);
				NewDiffusion->Index = Index;
				This->InitialDiffusions->Get(Scope)->Add(NewDiffusion);
			}
		};

		if (Context->SeedsDataFacade->GetNum() <= 0) { return false; }

		DiffusionInitialization->StartSubLoops(Context->SeedsDataFacade->GetNum(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);

#undef PCGEX_NEW_DIFFUSION

		return true;
	}

	void FProcessor::StartGrowth()
	{
		Seeded.Empty();

		InitialDiffusions->Collapse(OngoingDiffusions);
		InitialDiffusions.Reset();

		if (OngoingDiffusions.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A cluster could not initialize any diffusions. This is usually caused when there is more clusters than there is seeds, or all available seeds were better candidates for other clusters."));
			bIsProcessorValid = false;
			return;
		}

		// Prepare control handler before initializing diffusion
		// since the init does a first probing pass
		if (!FillControlsHandler->PrepareForDiffusions(OngoingDiffusions, Settings->Diffusion))
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Fill controls handler failed to prepare for diffusions. Check that all fill control inputs are valid."));
			bIsProcessorValid = false;
			return;
		}

		for (int i = 0; i < OngoingDiffusions.Num(); i++)
		{
			TSharedPtr<PCGExFloodFill::FDiffusion> Diffusion = OngoingDiffusions[i];
			const int32 InitIndex = Diffusion->Index;
			Diffusion->Index = i;
			Diffusion->Init(InitIndex);
		}

		Diffusions.Reserve(OngoingDiffusions.Num());

		Grow();
	}

	void FProcessor::Grow()
	{
		if (OngoingDiffusions.IsEmpty()) { return; }

		if (Settings->Processing == EPCGExFloodFillProcessing::Parallel)
		{
			// Grow all by a single step
			StartParallelLoopForRange(OngoingDiffusions.Num());
			return;
		}

		// Grow one entirely
		TSharedPtr<PCGExFloodFill::FDiffusion> Diffusion = OngoingDiffusions.Pop();
		while (!Diffusion->bStopped) { Diffusion->Grow(); }

		Diffusions.Add(Diffusion);

		Grow(); // Move to the next
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const TSharedPtr<PCGExFloodFill::FDiffusion> Diffusion = OngoingDiffusions[Index];
			const int32 CurrentFillRate = FillRate->Read(Diffusion->GetSettingsIndex(Settings->Diffusion.FillRateSource));
			for (int i = 0; i < CurrentFillRate; i++) { Diffusion->Grow(); }
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		// A single growth iteration pass is complete
		const int32 OngoingNum = OngoingDiffusions.Num();

		// Move stopped diffusions in another castle
		int32 WriteIndex = 0;
		for (int32 i = 0; i < OngoingNum; i++)
		{
			const TSharedPtr<PCGExFloodFill::FDiffusion> Diff = OngoingDiffusions[i];
			if (Diff->bStopped) { Diffusions.Add(Diff); }
			else { OngoingDiffusions[WriteIndex++] = Diff; }
		}

		OngoingDiffusions.SetNum(WriteIndex);

		if (OngoingDiffusions.IsEmpty()) { return; }

		Grow();
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
				PCGEX_OUTPUT_VALUE(DiffusionDistance, TargetIndex, Candidate.PathDistance);
				PCGEX_OUTPUT_VALUE(DiffusionOrder, TargetIndex, i);
				PCGEX_OUTPUT_VALUE(DiffusionEnding, TargetIndex, Diffusion->Endpoints.Contains(Candidate.CaptureIndex));
			}

			// Forward seed values to diffusion
			if (Diffusion->SeedIndex != -1) { Context->SeedForwardHandler->Forward(Diffusion->SeedIndex, VtxDataFacade, Indices); }
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
		PathWriter = MakeShared<PCGExFloodFill::FDiffusionPathWriter>(Cluster.ToSharedRef(), VtxDataFacade, Context->Paths.ToSharedRef());

		if (Settings->PathOutput == EPCGExFloodFillPathOutput::Full)
		{
			// Output full path, rather straightforward
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, PathsTaskGroup)
			PathsTaskGroup->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				TSharedPtr<PCGExFloodFill::FDiffusion> Diff = This->Diffusions[Index];
				for (const int32 EndpointIndex : Diff->Endpoints)
				{
					This->PathWriter->WriteFullPath(
						*Diff,
						Diff->Captured[EndpointIndex].Node->Index,
						This->Context->SeedAttributesToPathTags,
						This->Context->SeedsDataFacade.ToSharedRef());
				}
			};

			PathsTaskGroup->StartIterations(Diffusions.Num(), 1);
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, PathsTaskGroup)
		PathsTaskGroup->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, SortOver = Settings->PathPartitions, SortOrder = Settings->PartitionSorting](const int32 Index, const PCGExMT::FScope& Scope)
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
			case EPCGExFloodFillPathPartitions::Length: if (SortOrder == EPCGExSortDirection::Ascending) { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].PathDistance < Captured[B].PathDistance; }); }
				else { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].PathDistance > Captured[B].PathDistance; }); }
				break;
			case EPCGExFloodFillPathPartitions::Score: if (SortOrder == EPCGExSortDirection::Ascending) { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].PathScore < Captured[B].PathScore; }); }
				else { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].PathScore > Captured[B].PathScore; }); }
				break;
			case EPCGExFloodFillPathPartitions::Depth: if (SortOrder == EPCGExSortDirection::Ascending) { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].Depth < Captured[B].Depth; }); }
				else { Endpoints.Sort([&](const int32 A, const int32 B) { return Captured[A].Depth > Captured[B].Depth; }); }
				break;
			}

			for (const int32 EndpointIndex : Endpoints)
			{
				PathIndices.Reset();

				const int32 EndpointNodeIndex = Captured[EndpointIndex].Node->Index;

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

						if (bIsAlreadyVisited) { PathNodeIndex = -1; }
					}
				}

				This->PathWriter->WritePartitionedPath(
					*Diff,
					PathIndices,
					This->Context->SeedAttributesToPathTags,
					This->Context->SeedsDataFacade.ToSharedRef());
			}
		};

		PathsTaskGroup->StartIterations(Diffusions.Num(), 1);
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExClusterDiffusionContext, UPCGExClusterDiffusionSettings>::Cleanup();

		// Make sure we flush these ASAP
		InitialDiffusions.Reset();
		OngoingDiffusions.Reset();
		Diffusions.Reset();
		FillControlsHandler.Reset();
		BlendOpsManager.Reset();
		PathWriter.Reset();
	}


	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
		FPCGExClusterDiffusionContext* Ctx =static_cast<FPCGExClusterDiffusionContext*>(InContext); 
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

		// Diffusion rate

		FillRate = PCGExDetails::MakeSettingValue<int32>(Settings->Diffusion.FillRateInput, Settings->Diffusion.FillRateAttribute, Settings->Diffusion.FillRateConstant);
		bIsBatchValid = FillRate->Init(Settings->Diffusion.FillRateSource == EPCGExFloodFillSettingSource::Seed ? Context->SeedsDataFacade : VtxDataFacade);

		if (!bIsBatchValid) { return; } // Fail

		TBatch<FProcessor>::Process();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }

		PCGEX_TYPED_PROCESSOR

		TypedProcessor->BlendOpsManager = BlendOpsManager;
		TypedProcessor->InfluencesCount = InfluencesCount;

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
