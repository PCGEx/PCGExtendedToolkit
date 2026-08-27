// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClusterDecomposition.h"

#include "PCGParamData.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "ClusterDecomposition"
#define PCGEX_NAMESPACE ClusterDecomposition

bool UPCGExClusterDecompositionSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExHeuristics::Labels::SourceHeuristicsLabel)
	{
		return Decomposition && Decomposition->WantsHeuristics();
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExClusterDecompositionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	if (Decomposition && Decomposition->WantsHeuristics())
	{
		PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics may be required by some decompositions.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	}
	else
	{
		PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics may be required by some decompositions.", Advanced, FPCGExDataTypeInfoHeuristics::AsId())
	}

	PCGEX_PIN_OPERATION_OVERRIDES(PCGExClusterDecomposition::SourceOverridesDecomposition)

	return PinProperties;
}

PCGExData::EIOInit UPCGExClusterDecompositionSettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExClusterDecompositionSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGEX_INITIALIZE_ELEMENT(ClusterDecomposition)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ClusterDecomposition)

bool FPCGExClusterDecompositionElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDecomposition)

	if (!Settings->Decomposition)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No decomposition selected."));
		return false;
	}

	PCGEX_BIND_INSTANCED_FACTORY(Decomposition, UPCGExDecompositionInstancedFactory, PCGExClusterDecomposition::SourceOverridesDecomposition)

	if (Context->Decomposition->WantsHeuristics() && !Context->bHasValidHeuristics)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("The selected decomposition requires heuristics to be connected, but none can be found."));
		return false;
	}

	// Output attribute names must be distinct; a same-name collision silently clobbers data when
	// buffers flush (e.g. the FVector Cell Size over an int32 Cell ID / Cell Count of the same name).
	{
		const FName CellIDName = Settings->CellIDAttributeName;
		const FName CellCountName = Settings->CellCountAttributeName;
		const FName CellSizeName = Settings->CellSizeAttributeName;

		if ((CellSizeName != NAME_None && (CellSizeName == CellIDName || CellSizeName == CellCountName)) ||
			(CellCountName != NAME_None && CellCountName == CellIDName))
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("Cluster Decomposition output attribute names (Cell ID / Cell Count / Cell Size) are not distinct; same-named outputs will overwrite each other."));
		}
	}

	return true;
}

bool FPCGExClusterDecompositionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClusterDecompositionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDecomposition)
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
				if (Context->Decomposition->WantsHeuristics())
				{
					NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode);
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

namespace PCGExClusterDecomposition
{
#pragma region FProcessor

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExClusterDecomposition::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Operation = Context->Decomposition->CreateOperation();
		if (!Operation)
		{
			return false;
		}

		Operation->PrimaryDataFacade = VtxDataFacade;
		Operation->SecondaryDataFacade = EdgeDataFacade;

		Operation->PrepareForCluster(Cluster, HeuristicsHandler);

		FPCGExDecompositionResult Result;
		Result.Init(Cluster->Nodes->Num());
		Result.bWantsCellSizes = CellSizeBuffer != nullptr;

		if (Operation->DecomposeAndFinalize(Result))
		{
			const int32 Offset = EdgeDataFacade->Source->IOIndex * 1000000;
			// DecomposeAndFinalize guarantees CellSizes is sized to NumCells when bWantsCellSizes.
			const bool bWriteCellSize = Result.bWantsCellSizes;

			for (int32 NodeIndex = 0; NodeIndex < Result.NodeCellIDs.Num(); NodeIndex++)
			{
				const int32 CellID = Result.NodeCellIDs[NodeIndex];
				if (CellID < 0)
				{
					continue;
				}

				const int32 PointIndex = Cluster->GetNodePointIndex(NodeIndex);
				CellIDBuffer->SetValue(PointIndex, Offset + CellID);

				if (bWriteCellSize)
				{
					CellSizeBuffer->SetValue(PointIndex, Result.CellSizes[CellID]);
				}
			}

			if (CellCountBuffer)
			{
				// Count nodes per cell
				TMap<int32, int32> CellNodeCounts;
				for (int32 NodeIndex = 0; NodeIndex < Result.NodeCellIDs.Num(); NodeIndex++)
				{
					const int32 CellID = Result.NodeCellIDs[NodeIndex];
					if (CellID >= 0)
					{
						CellNodeCounts.FindOrAdd(CellID)++;
					}
				}

				// Write per-node cell count
				for (int32 NodeIndex = 0; NodeIndex < Result.NodeCellIDs.Num(); NodeIndex++)
				{
					const int32 CellID = Result.NodeCellIDs[NodeIndex];
					if (CellID < 0)
					{
						continue;
					}
					CellCountBuffer->SetValue(Cluster->GetNodePointIndex(NodeIndex), CellNodeCounts[CellID]);
				}
			}
		}
		else
		{
			// Buffers keep their defaults on failure, which is indistinguishable from a successful
			// single-cell decomposition -- say so rather than ship a graph that looks like it worked.
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Decomposition failed on a cluster; its Vtx keep the default cell values."));
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExClusterDecompositionContext, UPCGExClusterDecompositionSettings>::Cleanup();
		Operation.Reset();
	}

#pragma endregion

#pragma region FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDecomposition)

		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		Context->Decomposition->RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDecomposition)

		CellIDBuffer = VtxDataFacade->GetWritable<int32>(Settings->CellIDAttributeName, -1, true, PCGExData::EBufferInit::New);

		if (Settings->CellCountAttributeName != NAME_None)
		{
			CellCountBuffer = VtxDataFacade->GetWritable<int32>(Settings->CellCountAttributeName, 0, false, PCGExData::EBufferInit::New);
		}

		if (Settings->CellSizeAttributeName != NAME_None)
		{
			CellSizeBuffer = VtxDataFacade->GetWritable<FVector>(Settings->CellSizeAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::New);
		}

		Context->Decomposition->PrepareVtxFacade(VtxDataFacade);
		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}
		PCGEX_TYPED_PROCESSOR

		TypedProcessor->CellIDBuffer = CellIDBuffer;
		TypedProcessor->CellCountBuffer = CellCountBuffer;
		TypedProcessor->CellSizeBuffer = CellSizeBuffer;

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
