// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Paths/PCGExBreakClustersToPaths.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExChain.h"
#include "Clusters/Artifacts/PCGExCachedChain.h"
#include "Curve/CurveUtil.h"
#include "Data/PCGExData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExBreakClustersToPaths"
#define PCGEX_NAMESPACE BreakClustersToPaths

TArray<FPCGPinProperties> UPCGExBreakClustersToPathsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Paths", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExBreakClustersToPathsSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::NoInit; }
PCGExData::EIOInit UPCGExBreakClustersToPathsSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::NoInit; }

PCGEX_INITIALIZE_ELEMENT(BreakClustersToPaths)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(BreakClustersToPaths)

bool FPCGExBreakClustersToPathsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BreakClustersToPaths)

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExPaths::Labels::OutputPathsLabel;

	return true;
}

bool FPCGExBreakClustersToPathsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBreakClustersToPathsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BreakClustersToPaths)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		{
			if (Settings->Winding != EPCGExWindingMutation::Unchanged) { NewBatch->SetProjectionDetails(Settings->ProjectionDetails); }
			if (Settings->OperateOn == EPCGExBreakClusterOperationTarget::Paths)
			{
				NewBatch->VtxFilterFactories = &Context->FilterFactories;
			}
			else
			{
				NewBatch->bSkipCompletion = true;
			}
		}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPaths->StageOutputs();
	return Context->TryComplete();
}

namespace PCGExBreakClustersToPaths
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBreakClustersToPaths::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		if (!DirectionSettings.InitFromParent(ExecutionContext, GetParentBatch<FBatch>()->DirectionSettings, EdgeDataFacade)) { return false; }

		if (Settings->OperateOn == EPCGExBreakClusterOperationTarget::Paths)
		{
			if (VtxFiltersManager)
			{
				FilterVtxScope(PCGExMT::FScope(0, NumNodes), true);
				return BuildChains();
			}
			return BuildChains();
		}
		ChainsIO.Reserve(NumEdges);
		Context->OutputPaths->IncreaseReserve(NumEdges);
		for (int i = 0; i < NumEdges; ++i)
		{
			ChainsIO.Add(Context->OutputPaths->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New));
		}

		StartParallelLoopForEdges();

		return true;
	}

	bool FProcessor::BuildChains()
	{
		bIsProcessorValid = PCGExClusters::ChainHelpers::GetOrBuildChains(
			Cluster.ToSharedRef(),
			ProcessedChains,
			VtxFilterCache,
			Settings->LeavesHandling == EPCGExBreakClusterLeavesHandling::Only);

		return bIsProcessorValid;
	}


	void FProcessor::CompleteWork()
	{
		const int32 NumChains = ProcessedChains.Num();
		if (!NumChains)
		{
			bIsProcessorValid = false;
			return;
		}

		ChainsIO.Reserve(NumChains);
		Context->OutputPaths->IncreaseReserve(NumChains);
		for (int i = 0; i < NumChains; ++i)
		{
			ChainsIO.Add(Context->OutputPaths->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New));
		}

		StartParallelLoopForRange(NumChains);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const TSharedPtr<PCGExClusters::FNodeChain> Chain = ProcessedChains[Index];
			const TSharedPtr<PCGExData::FPointIO> PathIO = ChainsIO[Index];

			if (!Chain)
			{
				if (PathIO) { PathIO->Disable(); }
				continue;
			}

#define PCGEX_IGNORE_CHAIN PathIO->Disable(); continue;

			if (Settings->LeavesHandling == EPCGExBreakClusterLeavesHandling::Exclude && Chain->bIsLeaf) { PCGEX_IGNORE_CHAIN }

			const int32 ChainSize = Chain->Links.Num() + 1;

			if (ChainSize < Settings->MinPointCount) { PCGEX_IGNORE_CHAIN }
			if (Settings->bOmitAbovePointCount && ChainSize > Settings->MaxPointCount) { PCGEX_IGNORE_CHAIN }

			const bool bReverse = DirectionSettings.SortExtrapolation(Cluster.Get(), Chain->Seed.Edge, Chain->Seed.Node, Chain->Links.Last().Node);

			bool bDoReverse = bReverse;
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathIO->GetOut(), ChainSize, PathIO->GetOut()->GetAllocatedProperties());

			TArray<int32>& IdxMapping = PathIO->GetIdxMapping();
			IdxMapping[0] = Cluster->GetNodePointIndex(Chain->Seed);

			if (ProjectedVtxPositions && (!Settings->bWindOnlyClosedLoops || Chain->bIsClosedLoop))
			{
				const TArray<FVector2D>& PP = *ProjectedVtxPositions.Get();
				TArray<FVector2D> ProjectedPoints;
				ProjectedPoints.SetNumUninitialized(ChainSize);

				ProjectedPoints[0] = PP[Cluster->GetNodePointIndex(Chain->Seed)];

				for (int i = 1; i < ChainSize; i++)
				{
					const int32 PtIndex = Cluster->GetNodePointIndex(Chain->Links[i - 1]);
					IdxMapping[i] = PtIndex;
					ProjectedPoints[i] = PP[PtIndex];
				}

				if (!PCGExMath::IsWinded(Settings->Winding, UE::Geometry::CurveUtil::SignedArea2<double, FVector2D>(ProjectedPoints) < 0))
				{
					bDoReverse = true;
				}
			}
			else
			{
				for (int i = 1; i < ChainSize; i++) { IdxMapping[i] = Cluster->GetNodePointIndex(Chain->Links[i - 1]); }
			}

			if (bDoReverse) { Algo::Reverse(IdxMapping); }

			PCGExPaths::Helpers::SetClosedLoop(PathIO->GetOut(), Chain->bIsClosedLoop);

			PathIO->IOIndex = EdgeDataFacade->Source->IOIndex * 100000 + Cluster->GetNodePointIndex(FMath::Min(Chain->Links.Last().Node, Chain->Links[0].Node));
			PathIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);

#undef PCGX_IGNORE_CHAIN
		}
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];

			const TSharedPtr<PCGExData::FPointIO> PathIO = ChainsIO[Index];
			if (!PathIO) { continue; }

			UPCGBasePointData* MutablePoints = PathIO->GetOut();
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, 2, PathIO->GetAllocations());

			DirectionSettings.SortEndpoints(Cluster.Get(), Edge);

			TArray<int32>& IdxMapping = PathIO->GetIdxMapping();
			IdxMapping[0] = Edge.Start;
			IdxMapping[1] = Edge.End;

			PathIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);
			PCGExPaths::Helpers::SetClosedLoop(PathIO->GetOut(), false);
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExBreakClustersToPathsContext, UPCGExBreakClustersToPathsSettings>::Cleanup();
		ProcessedChains.Empty();
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(BreakClustersToPaths)
		DirectionSettings.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(BreakClustersToPaths)

		DirectionSettings = Settings->DirectionSettings;
		if (!DirectionSettings.Init(Context, VtxDataFacade, Context->GetEdgeSortingRules())) { return; }

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
