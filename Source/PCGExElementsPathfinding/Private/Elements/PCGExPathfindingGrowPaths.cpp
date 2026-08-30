// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingGrowPaths.h"


#include "PCGExHeuristicsHandler.h"
#include "Algo/Reverse.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Containers/PCGExHashLookup.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExPathfindingGrowPathsElement"
#define PCGEX_NAMESPACE PathfindingGrowPaths

#define PCGEX_GROWTH_GRAB(_CONTEXT, _TARGET, _SOURCE, _TYPE, _ATTRIBUTE) \
_TARGET = _SOURCE->GetBroadcaster<_TYPE>(_ATTRIBUTE); \
if (!_TARGET){	PCGE_LOG_C(Error, GraphAndLog, _CONTEXT, FTEXT("Missing specified " #_ATTRIBUTE " attribute."));	return false; }

#if WITH_EDITOR
void UPCGExPathfindingGrowPathsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

namespace PCGExPathfindingGrowPaths
{
	FGrowth::FGrowth(const TSharedPtr<FProcessor>& InProcessor, const int32 InMaxIterations, const int32 InLastGrowthIndex, const FVector& InGrowthDirection)
		: Processor(InProcessor)
		  , MaxIterations(InMaxIterations)
		  , LastGrowthIndex(InLastGrowthIndex)
		  , GrowthDirection(InGrowthDirection)
	{
		SoftMaxIterations = InMaxIterations;
		Path.Reserve(MaxIterations);
		Path.Add(InLastGrowthIndex);
		Init();
	}

	int32 FGrowth::FindNextGrowthNodeIndex()
	{
		if (Iteration + 1 > SoftMaxIterations)
		{
			NextGrowthIndex = -1;
			return NextGrowthIndex;
		}

		const TArray<PCGExClusters::FNode>& NodesRef = *Processor->Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& EdgesRef = *Processor->Cluster->Edges;

		const PCGExClusters::FNode& CurrentNode = NodesRef[LastGrowthIndex];

		double BestScore = TNumericLimits<double>::Max();
		NextGrowthIndex = -1;
		NextGrowthEdgeIndex = -1;

		for (const PCGExGraphs::FLink Lk : CurrentNode.Links)
		{
			const PCGExClusters::FNode& OtherNode = NodesRef[Lk.Node];

			if (Processor->GetSettings()->bUseNoGrowth)
			{
				bool bNoGrowth = Processor->NoGrowth ? Processor->NoGrowth->Read(OtherNode.PointIndex) : Processor->GetSettings()->bInvertNoGrowth;
				if (Processor->GetSettings()->bInvertNoGrowth)
				{
					bNoGrowth = !bNoGrowth;
				}

				if (bNoGrowth)
				{
					continue;
				}
			}

			if (Path.Contains(Lk.Node))
			{
				continue;
			}

			/*
			// TODO : Implement
			if (Settings->VisitedStopThreshold > 0 && Context->GlobalExtraWeights &&
				Context->GlobalExtraWeights->GetExtraWeight(AdjacentNodeIndex, Lk.Edge) > Settings->VisitedStopThreshold)
			{
				continue;
			}
			*/

			if (const double Score = GetGrowthScore(CurrentNode, OtherNode, EdgesRef[Lk.Edge]);
				Score < BestScore)
			{
				BestScore = Score;
				NextGrowthIndex = OtherNode.Index;
				NextGrowthEdgeIndex = Lk.Edge;
			}
		}

		return NextGrowthIndex;
	}

	bool FGrowth::Grow()
	{
		if (NextGrowthIndex <= -1 || Path.Contains(NextGrowthIndex))
		{
			return false;
		}

		TravelStack->Set(NextGrowthIndex, PCGEx::NH64(LastGrowthIndex, NextGrowthEdgeIndex));

		const TArray<PCGExClusters::FNode>& NodesRef = *Processor->Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& EdgesRef = *Processor->Cluster->Edges;

		const PCGExClusters::FNode& NextNode = NodesRef[NextGrowthIndex];

		Metrics.Add(Processor->Cluster->GetPos(NextNode));
		if (MaxDistance > 0 && Metrics.Length > MaxDistance)
		{
			return false;
		}


		Processor->HeuristicsHandler->FeedbackScore(NextNode, EdgesRef[NextGrowthEdgeIndex]);

		Iteration++;
		Path.Add(NextGrowthIndex);
		LastGrowthIndex = NextGrowthIndex;

		if (Processor->GetSettings()->NumIterations == EPCGExGrowthValueSource::VtxAttribute)
		{
			if (Processor->GetSettings()->NumIterationsUpdateMode == EPCGExGrowthUpdateMode::SetEachIteration)
			{
				SoftMaxIterations = Processor->NumIterations->Read(NextNode.PointIndex);
			}
			else if (Processor->GetSettings()->NumIterationsUpdateMode == EPCGExGrowthUpdateMode::AddEachIteration)
			{
				SoftMaxIterations += Processor->NumIterations->Read(NextNode.PointIndex);
			}
		}

		if (Processor->GetSettings()->GrowthDirection == EPCGExGrowthValueSource::VtxAttribute)
		{
			if (Processor->GetSettings()->GrowthDirectionUpdateMode == EPCGExGrowthUpdateMode::SetEachIteration)
			{
				GrowthDirection = Processor->GrowthDirection->Read(NextNode.PointIndex);
			}
			else if (Processor->GetSettings()->GrowthDirectionUpdateMode == EPCGExGrowthUpdateMode::AddEachIteration)
			{
				GrowthDirection = (GrowthDirection + Processor->GrowthDirection->Read(NextNode.PointIndex)).GetSafeNormal();
			}
		}

		//Processor->Cluster->VtxTransforms[GoalNode->Index] = Processor->Cluster->GetPos(NextNode) + GrowthDirection * 10000;

		if (Processor->GetSettings()->bUseGrowthStop)
		{
			bool bStopGrowth = Processor->GrowthStop ? Processor->GrowthStop->Read(NextNode.PointIndex) : Processor->GetSettings()->bInvertGrowthStop;
			if (Processor->GetSettings()->bInvertGrowthStop)
			{
				bStopGrowth = !bStopGrowth;
			}
			if (bStopGrowth)
			{
				SoftMaxIterations = -1;
			}
		}

		return true;
	}

	void FGrowth::Write()
	{
		const TSharedPtr<PCGExData::FPointIO> VtxIO = Processor->Cluster->VtxIO.Pin();
		const TSharedPtr<PCGExData::FPointIO> PathIO = Processor->GetContext()->OutputPaths->Emplace_GetRef<UPCGPointArrayData>(VtxIO->GetIn(), PCGExData::EIOInit::New);
		if (!VtxIO || !PathIO)
		{
			return;
		}

		PathIO->IOIndex = VtxIO->IOIndex * 100000 + SeedPointIndex;

		PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())

		PCGExClusters::Helpers::CleanupVtxData(PathIO);

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathIO->GetOut(), Path.Num());
		TArray<int32>& IdxMapping = PathIO->GetIdxMapping();

		for (int i = 0; i < Path.Num(); i++)
		{
			IdxMapping[i] = Processor->Cluster->GetNodePointIndex(Path[i]);
		}
		PathIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);

		PathIO->Tags->Append(VtxIO->Tags.ToSharedRef());

		FPCGExPathfindingGrowPathsContext* Context = Processor->GetContext();
		Context->SeedAttributesToPathTags.Tag(Context->SeedsDataFacade->GetInPoint(SeedPointIndex), PathIO);
		Context->SeedForwardHandler->Forward(SeedPointIndex, PathDataFacade);

		PathDataFacade->WriteFastest(Processor->TaskManager);
	}

	void FGrowth::Init()
	{
		SeedNode = &(*Processor->Cluster->Nodes)[LastGrowthIndex];
		Metrics.Reset(Processor->Cluster->GetPos(SeedNode));
		TravelStack = PCGEx::NewHashLookup<PCGEx::FHashLookupMap>(PCGEx::NH64(-1, -1), 0);
	}

	double FGrowth::GetGrowthScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge) const
	{
		return Processor->HeuristicsHandler->GetEdgeScore(From, To, Edge, *SeedNode, To, nullptr, TravelStack.Get());
	}
}

TArray<FPCGPinProperties> UPCGExPathfindingGrowPathsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points to start growth from.", Required)
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics.", Normal, FPCGExDataTypeInfoHeuristics::AsId())
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPathfindingGrowPathsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Paths output.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathfindingGrowPaths)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(PathfindingGrowPaths)

bool FPCGExPathfindingGrowPathsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingGrowPaths)

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);

	if (Settings->NumIterations == EPCGExGrowthValueSource::SeedAttribute)
	{
		PCGEX_GROWTH_GRAB(Context, Context->NumIterations, Context->SeedsDataFacade, int32, Settings->NumIterationsAttribute)
	}

	if (Settings->SeedNumBranches == EPCGExGrowthValueSource::SeedAttribute)
	{
		PCGEX_GROWTH_GRAB(Context, Context->NumBranches, Context->SeedsDataFacade, int32, Settings->NumBranchesAttribute)
	}

	if (Settings->GrowthDirection == EPCGExGrowthValueSource::SeedAttribute)
	{
		PCGEX_GROWTH_GRAB(Context, Context->GrowthDirection, Context->SeedsDataFacade, FVector, Settings->GrowthDirectionAttribute)
	}

	if (Settings->GrowthMaxDistance == EPCGExGrowthValueSource::SeedAttribute)
	{
		PCGEX_GROWTH_GRAB(Context, Context->GrowthMaxDistance, Context->SeedsDataFacade, double, Settings->GrowthMaxDistanceAttribute)
	}

	PCGEX_FWD(SeedAttributesToPathTags)

	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}
	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);

	return true;
}

bool FPCGExPathfindingGrowPathsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathfindingGrowPathsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingGrowPaths)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPaths->StageOutputs();

	return Context->TryComplete();
}


namespace PCGExPathfindingGrowPaths
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathfindingGrowPaths::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Prepare getters

		if (Settings->NumIterations == EPCGExGrowthValueSource::VtxAttribute)
		{
			PCGEX_GROWTH_GRAB(ExecutionContext, NumIterations, VtxDataFacade, int32, Settings->NumIterationsAttribute)
		}

		if (Settings->SeedNumBranches == EPCGExGrowthValueSource::VtxAttribute)
		{
			PCGEX_GROWTH_GRAB(ExecutionContext, NumBranches, VtxDataFacade, int32, Settings->NumBranchesAttribute)
		}

		if (Settings->GrowthDirection == EPCGExGrowthValueSource::VtxAttribute)
		{
			PCGEX_GROWTH_GRAB(ExecutionContext, GrowthDirection, VtxDataFacade, FVector, Settings->GrowthDirectionAttribute)
		}

		if (Settings->GrowthMaxDistance == EPCGExGrowthValueSource::VtxAttribute)
		{
			PCGEX_GROWTH_GRAB(ExecutionContext, GrowthMaxDistance, VtxDataFacade, double, Settings->GrowthMaxDistanceAttribute)
		}

		GrowthStop = Settings->bUseGrowthStop ? VtxDataFacade->GetBroadcaster<bool>(Settings->GrowthStopAttribute) : nullptr;
		NoGrowth = Settings->bUseNoGrowth ? VtxDataFacade->GetBroadcaster<bool>(Settings->NoGrowthAttribute) : nullptr;

		// Growth re-scores every neighbor on every iteration -- always worth baking static edge scores.
		HeuristicsHandler->BakeStaticEdgeScores();

		if (Settings->bUseOctreeSearch)
		{
			Cluster->RebuildOctree(Settings->SeedPicking.PickingMethod);
		}

		// Prepare growth points

		PCGEX_SHARED_THIS_DECL

		// Find all growth points
		const int32 SeedCount = Context->SeedsDataFacade->Source->GetNum();
		for (int i = 0; i < SeedCount; i++)
		{
			const FVector SeedPosition = Context->SeedsDataFacade->Source->GetInPoint(i).GetLocation();
			const int32 NodeIndex = Settings->SeedPicking.PickClosestNode(*Cluster, SeedPosition);

			if (NodeIndex == -1)
			{
				continue;
			}

			const PCGExClusters::FNode& Node = (*Cluster->Nodes)[NodeIndex];
			if (Node.IsEmpty())
			{
				continue;
			}

			double StartNumIterations;
			double StartGrowthNumBranches;
			FVector StartGrowthDirection = FVector::UpVector;
			double StartGrowthMaxDistance;

			switch (Settings->SeedNumBranches)
			{
			default: ;
			case EPCGExGrowthValueSource::Constant:
				StartGrowthNumBranches = Settings->NumBranchesConstant;
				break;
			case EPCGExGrowthValueSource::SeedAttribute:
				StartGrowthNumBranches = Context->NumBranches->Read(i);
				break;
			case EPCGExGrowthValueSource::VtxAttribute:
				StartGrowthNumBranches = NumBranches->Read(Node.PointIndex);
				break;
			}

			switch (Settings->NumIterations)
			{
			default: ;
			case EPCGExGrowthValueSource::Constant:
				StartNumIterations = Settings->NumIterationsConstant;
				break;
			case EPCGExGrowthValueSource::SeedAttribute:
				StartNumIterations = Context->NumIterations->Read(i);
				break;
			case EPCGExGrowthValueSource::VtxAttribute:
				StartNumIterations = NumIterations->Read(Node.PointIndex);
				break;
			}

			switch (Settings->GrowthMaxDistance)
			{
			default: ;
			case EPCGExGrowthValueSource::Constant:
				StartGrowthMaxDistance = Settings->GrowthMaxDistanceConstant;
				break;
			case EPCGExGrowthValueSource::SeedAttribute:
				StartGrowthMaxDistance = Context->GrowthMaxDistance->Read(i);
				break;
			case EPCGExGrowthValueSource::VtxAttribute:
				StartGrowthMaxDistance = GrowthMaxDistance->Read(Node.PointIndex);
				break;
			}

			switch (Settings->GrowthDirection)
			{
			default: ;
			case EPCGExGrowthValueSource::Constant:
				StartGrowthDirection = Settings->GrowthDirectionConstant;
				break;
			case EPCGExGrowthValueSource::SeedAttribute:
				StartGrowthDirection = Context->GrowthDirection->Read(i);
				break;
			case EPCGExGrowthValueSource::VtxAttribute:
				StartGrowthDirection = GrowthDirection->Read(Node.PointIndex);
				break;
			}

			if (StartGrowthNumBranches <= 0 || StartNumIterations <= 0)
			{
				continue;
			}

			if (Settings->SeedNumBranchesMean == EPCGExMeanMeasure::Relative)
			{
				StartGrowthNumBranches = FMath::Max(1, static_cast<double>(Node.Num()) * StartGrowthNumBranches);
			}

			for (int j = 0; j < StartGrowthNumBranches; j++)
			{
				PCGEX_MAKE_SHARED(NewGrowth, FGrowth, ThisPtr, StartNumIterations, Node.Index, StartGrowthDirection)
				NewGrowth->MaxDistance = StartGrowthMaxDistance;
				NewGrowth->SeedPointIndex = i;

				if (!(NewGrowth->FindNextGrowthNodeIndex() != -1 && NewGrowth->Grow()))
				{
					continue;
				}

				Growths.Add(NewGrowth);
				QueuedGrowths.Add(NewGrowth);
			}
		}

		PCGEX_LAUNCH(FGrowTask, ThisPtr)
		return true;
	}

	void FProcessor::CompleteWork()
	{
		for (const TSharedPtr<FGrowth>& Growth : Growths)
		{
			Growth->Write();
		}
	}

	void FProcessor::Grow()
	{
		if (Settings->GrowthMode == EPCGExGrowthIterationMode::Parallel)
		{
			for (const TSharedPtr<FGrowth>& Growth : QueuedGrowths)
			{
				while (Growth->FindNextGrowthNodeIndex() != -1 && Growth->Grow())
				{
				}
			}

			QueuedGrowths.Empty();
		}
		else
		{
			while (!QueuedGrowths.IsEmpty())
			{
				for (int i = 0; i < QueuedGrowths.Num(); i++)
				{
					const TSharedPtr<FGrowth>& Growth = QueuedGrowths[i];

					Growth->FindNextGrowthNodeIndex();

					if (!Growth->Grow())
					{
						QueuedGrowths.RemoveAt(i);
						i--;
					}
				}
			}
		}
	}

	void FGrowTask::ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
	{
		Processor->Grow();
	}
}

#undef PCGEX_GROWTH_GRAB

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
