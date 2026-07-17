// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClusterCentrality.h"

#include "PCGExHeuristicsHandler.h"
#include "PCGParamData.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Utils/PCGExScoredQueue.h"

#define LOCTEXT_NAMESPACE "PCGExClusterCentralityElement"
#define PCGEX_NAMESPACE ClusterCentrality

PCGEX_INITIALIZE_ELEMENT(ClusterCentrality)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ClusterCentrality)

#if WITH_EDITOR
void UPCGExClusterCentralitySettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 0)
	{
		RandomDownsampling.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExClusterCentralitySettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 0)
	{
		RandomDownsampling.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool UPCGExClusterCentralitySettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExClusters::Labels::SourceVtxFiltersLabel)
	{
		return IsPathBased() && DownsamplingMode == EPCGExCentralityDownsampling::Filters;
	}
	if (InPin->Properties.Label == PCGExHeuristics::Labels::SourceHeuristicsLabel)
	{
		return IsPathBased();
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExClusterCentralitySettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	if (IsPathBased())
	{
		PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	}
	else
	{
		PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics.", Advanced, FPCGExDataTypeInfoHeuristics::AsId())
	}

	if (IsPathBased() && DownsamplingMode == EPCGExCentralityDownsampling::Filters)
	{
		PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceVtxFiltersLabel, "Vtx filters.", Required)
	}
	else
	{
		PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceVtxFiltersLabel, "Vtx filters.", Advanced)
	}

	return PinProperties;
}

PCGExData::EIOInit UPCGExClusterCentralitySettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExClusterCentralitySettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

bool FPCGExClusterCentralityElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ClusterCentrality)

	PCGEX_VALIDATE_NAME(Settings->CentralityValueAttributeName)

	if (Settings->IsPathBased() && Settings->DownsamplingMode == EPCGExCentralityDownsampling::Filters)
	{
		if (!PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceVtxFiltersLabel, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters()))
		{
			return false;
		}
	}

	return true;
}

bool FPCGExClusterCentralityElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClusterCentralityElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ClusterCentrality)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				if (Settings->IsPathBased())
				{
					NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode);
				}
				NewBatch->bSkipCompletion = true;
				NewBatch->bRequiresWriteStep = true;
				if (Settings->IsPathBased() && Settings->DownsamplingMode == EPCGExCentralityDownsampling::Filters)
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

namespace PCGExClusterCentrality
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExClusterCentrality::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		CentralityScores.Init(0.0, NumNodes);

		// Degree centrality: compute directly, no Dijkstra needed
		if (Settings->CentralityType == EPCGExCentralityType::Degree)
		{
			const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
			for (int32 i = 0; i < NumNodes; i++)
			{
				CentralityScores[i] = static_cast<double>(Nodes[i].Links.Num());
			}
			WriteResults();
			return true;
		}

		// Eigenvector/Katz: compute directly from adjacency, no edge scores needed
		if (Settings->CentralityType == EPCGExCentralityType::Eigenvector)
		{
			ComputeEigenvector();
			WriteResults();
			return true;
		}

		if (Settings->CentralityType == EPCGExCentralityType::Katz)
		{
			ComputeKatz();
			WriteResults();
			return true;
		}

		// Path-based types: need edge scores + optional downsampling
		bDownsample = Settings->DownsamplingMode != EPCGExCentralityDownsampling::None;
		if (bDownsample)
		{
			if (Settings->DownsamplingMode == EPCGExCentralityDownsampling::Ratio)
			{
				Settings->RandomDownsampling.GetPicks(Context, VtxDataFacade->GetIn(), NumNodes, RandomSamples);
			}
			else
			{
				PCGExArrayHelpers::ArrayOfIndices(RandomSamples, NumNodes);
				bVtxComplete = false;
				StartParallelLoopForNodes();
			}
		}

		DirectedEdgeScores.SetNum(NumEdges * 2);
		StartParallelLoopForEdges();

		return true;
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const PCGExClusters::FEdge& Edge = *Cluster->GetEdge(Index);
			const PCGExClusters::FNode& Start = *Cluster->GetEdgeStart(Edge);
			const PCGExClusters::FNode& End = *Cluster->GetEdgeEnd(Edge);

			DirectedEdgeScores[Index] = HeuristicsHandler->GetEdgeScore(Start, End, Edge, Start, End, nullptr, nullptr);

			DirectedEdgeScores[NumEdges + Index] = HeuristicsHandler->GetEdgeScore(End, Start, Edge, End, Start, nullptr, nullptr);
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		{
			FWriteScopeLock WriteScopeLock(CompletionLock);
			bEdgeComplete = true;
		}

		TryStartCompute();
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExClusterCentrality::ProcessNodes);
		FilterVtxScope(Scope);
	}

	void FProcessor::OnNodesProcessingComplete()
	{
		{
			FWriteScopeLock WriteScopeLock(CompletionLock);
			bVtxComplete = true;
		}

		TryStartCompute();
	}

	void FProcessor::TryStartCompute()
	{
		{
			FReadScopeLock ReadScopeLock(CompletionLock);
			if (!bVtxComplete || !bEdgeComplete)
			{
				return;
			}
		}

		if (Settings->DownsamplingMode == EPCGExCentralityDownsampling::Filters)
		{
			int32 WriteIndex = 0;
			for (int i = 0; i < NumNodes; i++)
			{
				if (IsNodePassingFilters(*(Cluster->Nodes->GetData() + i)))
				{
					RandomSamples[WriteIndex++] = i;
				}
			}

			RandomSamples.SetNum(WriteIndex);
		}

		if (bDownsample && RandomSamples.IsEmpty())
		{
			RandomSamples.Add(0);
		}

		StartParallelLoopForRange(bDownsample ? RandomSamples.Num() : NumNodes, 128);
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedCentralityScores = MakeShared<PCGExMT::TScopedArray<double>>(Loops);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExClusterCentrality::ProcessRange);

		TArray<double>& LocalScores = ScopedCentralityScores->Get_Ref(Scope);
		LocalScores.Init(0.0, NumNodes);

		TArray<double> Score;
		Score.Init(DBL_MAX, NumNodes);

		TArray<int32> Stack;
		Stack.Reserve(NumNodes);

		TSharedPtr<PCGEx::FScoredQueue> Queue = MakeShared<PCGEx::FScoredQueue>(NumNodes);

		if (Settings->CentralityType == EPCGExCentralityType::Betweenness)
		{
			TArray<double> Sigma;
			Sigma.Init(0.0, NumNodes);

			TArray<double> Delta;
			Delta.Init(0.0, NumNodes);

			TArray<NodePred> Pred;
			Pred.SetNum(NumNodes);

			if (bDownsample)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_Betweenness(RandomSamples[Index], LocalScores, Score, Sigma, Delta, Pred, Stack, Queue);
				}

				const double Ratio = static_cast<double>(NumNodes) / static_cast<double>(RandomSamples.Num());
				for (double& B : LocalScores)
				{
					B *= Ratio;
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_Betweenness(Index, LocalScores, Score, Sigma, Delta, Pred, Stack, Queue);
				}
			}
		}
		else if (Settings->CentralityType == EPCGExCentralityType::Closeness)
		{
			if (bDownsample)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_Closeness(RandomSamples[Index], LocalScores, Score, Stack, Queue);
				}

				const double Ratio = static_cast<double>(NumNodes) / static_cast<double>(RandomSamples.Num());
				for (double& B : LocalScores)
				{
					B *= Ratio;
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_Closeness(Index, LocalScores, Score, Stack, Queue);
				}
			}
		}
		else if (Settings->CentralityType == EPCGExCentralityType::HarmonicCloseness)
		{
			if (bDownsample)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_HarmonicCloseness(RandomSamples[Index], LocalScores, Score, Stack, Queue);
				}

				const double Ratio = static_cast<double>(NumNodes) / static_cast<double>(RandomSamples.Num());
				for (double& B : LocalScores)
				{
					B *= Ratio;
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					ProcessSingleNode_HarmonicCloseness(Index, LocalScores, Score, Stack, Queue);
				}
			}
		}
	}

#pragma region ProcessSingleNode_Betweenness

	void FProcessor::ProcessSingleNode_Betweenness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<double>& Sigma, TArray<double>& Delta, TArray<NodePred>& Pred, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue)
	{
		Stack.Reset();

		Score[Index] = 0.0;
		Sigma[Index] = 1.0;

		Queue->Reset();
		Queue->Enqueue(Index, 0.0);

		int32 CurrentNode;
		double CurrentScore;

		while (Queue->Dequeue(CurrentNode, CurrentScore))
		{
			Stack.Add(CurrentNode);
			const PCGExClusters::FNode& Current = *Cluster->GetNode(CurrentNode);

			for (const PCGExGraphs::FLink Lk : Current.Links)
			{
				const int32 Neighbor = Lk.Node;
				const int32 EdgeIndex = Lk.Edge;
				const PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(EdgeIndex);

				const double EdgeCost = Edge.Start == Current.PointIndex ? DirectedEdgeScores[EdgeIndex] : DirectedEdgeScores[NumEdges + EdgeIndex];
				const double NewDist = Score[CurrentNode] + EdgeCost;

				if (NewDist < Score[Neighbor])
				{
					Score[Neighbor] = NewDist;
					Queue->Enqueue(Neighbor, NewDist);
					Pred[Neighbor].Reset();
					Pred[Neighbor].Add(CurrentNode);
					Sigma[Neighbor] = Sigma[CurrentNode];
				}
				else if (FMath::IsNearlyEqual(NewDist, Score[Neighbor]))
				{
					Pred[Neighbor].Add(CurrentNode);
					Sigma[Neighbor] += Sigma[CurrentNode];
				}
			}
		}

		// Accumulate dependencies
		for (int32 i = Stack.Num() - 1; i >= 0; --i)
		{
			const int32 W = Stack[i];
			for (const int32 V : Pred[W])
			{
				Delta[V] += (Sigma[V] / Sigma[W]) * (1.0 + Delta[W]);
			}
			if (W != Index)
			{
				LocalScores[W] += Delta[W];
			}
		}

		// Reset only visited nodes (optimization: O(visited) instead of O(N))
		for (const int32 N : Stack)
		{
			Score[N] = DBL_MAX;
			Sigma[N] = 0;
			Delta[N] = 0;
			Pred[N].Reset();
		}
	}

#pragma endregion

#pragma region ProcessSingleNode_Closeness

	void FProcessor::ProcessSingleNode_Closeness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue)
	{
		Stack.Reset();

		Score[Index] = 0.0;

		Queue->Reset();
		Queue->Enqueue(Index, 0.0);

		int32 CurrentNode;
		double CurrentScore;

		while (Queue->Dequeue(CurrentNode, CurrentScore))
		{
			Stack.Add(CurrentNode);
			const PCGExClusters::FNode& Current = *Cluster->GetNode(CurrentNode);

			for (const PCGExGraphs::FLink Lk : Current.Links)
			{
				const int32 Neighbor = Lk.Node;
				const int32 EdgeIndex = Lk.Edge;
				const PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(EdgeIndex);

				const double EdgeCost = Edge.Start == Current.PointIndex ? DirectedEdgeScores[EdgeIndex] : DirectedEdgeScores[NumEdges + EdgeIndex];
				const double NewDist = Score[CurrentNode] + EdgeCost;

				if (NewDist < Score[Neighbor])
				{
					Score[Neighbor] = NewDist;
					Queue->Enqueue(Neighbor, NewDist);
				}
			}
		}

		// Accumulate closeness: reachable / sum_dist
		double SumDist = 0;
		int32 Reachable = 0;
		for (const int32 N : Stack)
		{
			if (N != Index)
			{
				SumDist += Score[N];
				Reachable++;
			}
		}

		if (SumDist > 0)
		{
			LocalScores[Index] += static_cast<double>(Reachable) / SumDist;
		}

		// Reset only visited nodes
		for (const int32 N : Stack)
		{
			Score[N] = DBL_MAX;
		}
	}

#pragma endregion

#pragma region ProcessSingleNode_HarmonicCloseness

	void FProcessor::ProcessSingleNode_HarmonicCloseness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue)
	{
		Stack.Reset();

		Score[Index] = 0.0;

		Queue->Reset();
		Queue->Enqueue(Index, 0.0);

		int32 CurrentNode;
		double CurrentScore;

		while (Queue->Dequeue(CurrentNode, CurrentScore))
		{
			Stack.Add(CurrentNode);
			const PCGExClusters::FNode& Current = *Cluster->GetNode(CurrentNode);

			for (const PCGExGraphs::FLink Lk : Current.Links)
			{
				const int32 Neighbor = Lk.Node;
				const int32 EdgeIndex = Lk.Edge;
				const PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(EdgeIndex);

				const double EdgeCost = Edge.Start == Current.PointIndex ? DirectedEdgeScores[EdgeIndex] : DirectedEdgeScores[NumEdges + EdgeIndex];
				const double NewDist = Score[CurrentNode] + EdgeCost;

				if (NewDist < Score[Neighbor])
				{
					Score[Neighbor] = NewDist;
					Queue->Enqueue(Neighbor, NewDist);
				}
			}
		}

		// Accumulate harmonic closeness: sum(1/distance)
		double HarmonicSum = 0;
		for (const int32 N : Stack)
		{
			if (N != Index && Score[N] > 0)
			{
				HarmonicSum += 1.0 / Score[N];
			}
		}

		LocalScores[Index] += HarmonicSum;

		// Reset only visited nodes
		for (const int32 N : Stack)
		{
			Score[N] = DBL_MAX;
		}
	}

#pragma endregion

#pragma region ComputeEigenvector

	void FProcessor::ComputeEigenvector()
	{
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
		const double InitVal = 1.0 / FMath::Sqrt(static_cast<double>(NumNodes));

		TArray<double> X;
		X.Init(InitVal, NumNodes);

		TArray<double> XNew;
		XNew.SetNum(NumNodes);

		for (int32 Iter = 0; Iter < Settings->MaxIterations; Iter++)
		{
			// x_new[i] = sum of x[neighbor] for each neighbor of i
			for (int32 i = 0; i < NumNodes; i++)
			{
				double Sum = 0;
				for (const PCGExGraphs::FLink Lk : Nodes[i].Links)
				{
					Sum += X[Lk.Node];
				}
				XNew[i] = Sum;
			}

			// Normalize: norm = ||x_new||_2
			double Norm = 0;
			for (int32 i = 0; i < NumNodes; i++)
			{
				Norm += XNew[i] * XNew[i];
			}
			Norm = FMath::Sqrt(Norm);

			if (Norm > 0)
			{
				for (int32 i = 0; i < NumNodes; i++)
				{
					XNew[i] /= Norm;
				}
			}

			// Check convergence: ||x_new - x||_2
			double Diff = 0;
			for (int32 i = 0; i < NumNodes; i++)
			{
				const double D = XNew[i] - X[i];
				Diff += D * D;
			}

			Swap(X, XNew);

			if (FMath::Sqrt(Diff) < Settings->Tolerance)
			{
				break;
			}
		}

		for (int32 i = 0; i < NumNodes; i++)
		{
			CentralityScores[i] = X[i];
		}
	}

#pragma endregion

#pragma region ComputeKatz

	void FProcessor::ComputeKatz()
	{
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
		const double Alpha = Settings->KatzAlpha;

		TArray<double> X;
		X.Init(1.0, NumNodes);

		TArray<double> XNew;
		XNew.SetNum(NumNodes);

		for (int32 Iter = 0; Iter < Settings->MaxIterations; Iter++)
		{
			// x_new[i] = alpha * sum(x[neighbor]) + 1.0
			for (int32 i = 0; i < NumNodes; i++)
			{
				double Sum = 0;
				for (const PCGExGraphs::FLink Lk : Nodes[i].Links)
				{
					Sum += X[Lk.Node];
				}
				XNew[i] = Alpha * Sum + 1.0;
			}

			// Check convergence: ||x_new - x||_inf
			double MaxDiff = 0;
			for (int32 i = 0; i < NumNodes; i++)
			{
				MaxDiff = FMath::Max(MaxDiff, FMath::Abs(XNew[i] - X[i]));
			}

			Swap(X, XNew);

			if (MaxDiff < Settings->Tolerance)
			{
				break;
			}
		}

		for (int32 i = 0; i < NumNodes; i++)
		{
			CentralityScores[i] = X[i];
		}
	}

#pragma endregion

#pragma region FProcessor

	void FProcessor::OnRangeProcessingComplete()
	{
		ScopedCentralityScores->ForEach([&](TArray<double>& ScopedArray)
		{
			for (int i = 0; i < NumNodes; i++)
			{
				CentralityScores[i] += ScopedArray[i];
			}
			ScopedArray.Empty();
		});

		ScopedCentralityScores.Reset();

		// Normalize for undirected graphs (betweenness only)
		if (Settings->CentralityType == EPCGExCentralityType::Betweenness)
		{
			for (double& C : CentralityScores)
			{
				C *= 0.5;
			}
		}

		WriteResults();
	}

	void FProcessor::WriteResults()
	{
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
		TSharedPtr<PCGExData::TBuffer<double>> Buffer = VtxDataFacade->GetWritable<double>(Settings->CentralityValueAttributeName, Settings->bOutputOneMinus ? 1 : 0, true, PCGExData::EBufferInit::New);

		// Compute output values into CentralityScores (reuse the array)
		double Max = 0;
		for (const double& C : CentralityScores)
		{
			Max = FMath::Max(Max, C);
		}

		if (Settings->bNormalize && Max > 0)
		{
			const double InvMax = 1.0 / Max;
			if (Settings->bOutputOneMinus)
			{
				for (int i = 0; i < NumNodes; i++)
				{
					CentralityScores[i] = 1.0 - (CentralityScores[i] * InvMax);
				}
			}
			else
			{
				for (int i = 0; i < NumNodes; i++)
				{
					CentralityScores[i] *= InvMax;
				}
			}
		}
		else if (Settings->bNormalize)
		{
			const double V = Settings->bOutputOneMinus ? 1.0 : 0.0;
			for (int i = 0; i < NumNodes; i++)
			{
				CentralityScores[i] = V;
			}
		}

		// Apply contrast per-cluster on computed values
		if (Settings->bApplyContrast)
		{
			double RangeMin = CentralityScores[0];
			double RangeMax = CentralityScores[0];
			for (int i = 1; i < NumNodes; i++)
			{
				RangeMin = FMath::Min(RangeMin, CentralityScores[i]);
				RangeMax = FMath::Max(RangeMax, CentralityScores[i]);
			}

			if (RangeMax > RangeMin + SMALL_NUMBER)
			{
				const int32 CurveType = static_cast<int32>(Settings->ContrastCurve);
				for (int i = 0; i < NumNodes; i++)
				{
					CentralityScores[i] = PCGExMath::Contrast::ApplyContrastInRange(CentralityScores[i], Settings->ContrastAmount, CurveType, RangeMin, RangeMax);
				}
			}
		}

		// Write final values to buffer
		for (int i = 0; i < NumNodes; i++)
		{
			Buffer->SetValue(Nodes[i].PointIndex, CentralityScores[i]);
		}
	}

#pragma endregion

#pragma region FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	void FBatch::Write()
	{
		VtxDataFacade->WriteFastest(TaskManager);
	}

#pragma endregion
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
