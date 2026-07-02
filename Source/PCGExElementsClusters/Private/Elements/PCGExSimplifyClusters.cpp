// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSimplifyClusters.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExCachedChain.h"
#include "Clusters/Artifacts/PCGExChain.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExChainHelpers.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EIOInit UPCGExSimplifyClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::New;
}

PCGExData::EIOInit UPCGExSimplifyClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

#pragma endregion
TArray<FPCGPinProperties> UPCGExSimplifyClustersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeFiltersLabel, "Optional edge filters.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SimplifyClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(SimplifyClusters)

bool FPCGExSimplifyClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SimplifyClusters)

	PCGEX_FWD(GraphBuilderDetails)
	PCGEX_FWD(EdgeCarryOverDetails)

	Context->EdgeCarryOverDetails.Init();

	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceEdgeFiltersLabel, Context->EdgeFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);

	return true;
}

bool FPCGExSimplifyClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSimplifyClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SimplifyClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([&](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
		                                      {
			                                      return true;
		                                      }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		                                      {
		                                      }))
		{
			Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExGraphs::States::State_ReadyToCompile)
	if (!Context->CompileGraphBuilders(true, PCGExCommon::States::State_Done))
	{
		return false;
	}
	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSimplifyClusters
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSimplifyClusters::Process);

		if (Settings->bMergeAboveAngularThreshold && Settings->bFuseCollocated)
		{
			FuseDistance = FMath::Square(Settings->FuseDistance);
		}

		EdgeDataFacade->bSupportsScopedGet = true;
		if (!Context->EdgeFilterFactories.IsEmpty())
		{
			EdgeFilterFactories = &Context->EdgeFilterFactories;
		}

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (!Context->EdgeFilterFactories.IsEmpty())
		{
			StartParallelLoopForEdges();
		}
		else
		{
			CompileChains();
		}

		return true;
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		EdgeDataFacade->Fetch(Scope);
		FilterEdgeScope(Scope);

		TArray<int8>& BreakpointsRef = *Breakpoints.Get();

		if (Settings->EdgeFilterRole == EPCGExSimplifyClusterEdgeFilterRole::Collapse)
		{
			// Collapse endpoints
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!EdgeFilterCache[Index])
				{
					continue;
				}
				PCGExGraphs::FEdge* Edge = Cluster->GetEdge(Index);
				FPlatformAtomics::InterlockedExchange(&BreakpointsRef[Edge->Start], 0);
				FPlatformAtomics::InterlockedExchange(&BreakpointsRef[Edge->End], 0);
			}
		}
		else
		{
			// Restore endpoints
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!EdgeFilterCache[Index])
				{
					continue;
				}
				PCGExGraphs::FEdge* Edge = Cluster->GetEdge(Index);
				FPlatformAtomics::InterlockedExchange(&BreakpointsRef[Edge->Start], 1);
				FPlatformAtomics::InterlockedExchange(&BreakpointsRef[Edge->End], 1);
			}
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		CompileChains();
	}

	void FProcessor::CompileChains()
	{
		// SimplifyClusters' FBatch creates the EdgesUnion as a concrete FUnionMetadata (sparse, mutable),
		// so this downcast from the interface-typed FGraph field is sound.
		EdgesUnion = StaticCastSharedPtr<PCGExData::FUnionMetadata>(GraphBuilder->Graph->EdgesUnion);

		bIsProcessorValid = PCGExClusters::ChainHelpers::GetOrBuildChains(
			Cluster.ToSharedRef(),
			ProcessedChains,
			Breakpoints,
			false);
	}

	void FProcessor::CompleteWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSimplifyClusters::FProcessor::CompleteWork);
		StartParallelLoopForRange(ProcessedChains.Num());
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const TSharedPtr<PCGExClusters::FNodeChain> Chain = ProcessedChains[Index];
			if (!Chain)
			{
				continue;
			}

			if (Settings->bPruneLeaves && Chain->bIsLeaf)
			{
				continue;
			} // Skip leaf

			const bool bComputeMeta = Settings->EdgeUnionData.WriteAny();

			if (Settings->bOperateOnLeavesOnly && !Chain->bIsLeaf)
			{
				PCGExClusters::ChainHelpers::Dump(Chain.ToSharedRef(), Cluster.ToSharedRef(), GraphBuilder->Graph, bComputeMeta);
				continue;
			}

			if (Chain->SingleEdge != -1 || !Settings->bMergeAboveAngularThreshold)
			{
				// TODO : When using reduced dump we know in advance the number of edges will be the number of chains (optionally minus leaves)
				// We can pre-populate the graph union data
				PCGExClusters::ChainHelpers::DumpReduced(Chain.ToSharedRef(), Cluster.ToSharedRef(), GraphBuilder->Graph, bComputeMeta);
				continue;
			}

			const double DotThreshold = PCGExMath::DegreesToDot(Settings->AngularThreshold);
			const int32 IOIndex = EdgeDataFacade->Source->IOIndex;

			const TArray<PCGExGraphs::FLink>& Links = Chain->Links;
			const int32 MaxIndex = Links.Num() - 1;

			TArray<int32> PendingEdges; // Original edges absorbed since the last kept node
			PendingEdges.Reserve(Links.Num() + 1);

			// Collinear (mergeable) when the turn between incoming and outgoing directions is within
			// threshold -- a local property of immediate neighbors. Coincident points give a zero
			// direction and are always mergeable.
			auto IsCollinear = [&](const int32 PrevNode, const int32 CurNode, const int32 NextNode) -> bool
			{
				const FVector A = Cluster->GetDir(PrevNode, CurNode);
				const FVector B = Cluster->GetDir(CurNode, NextNode);
				if (A.IsNearlyZero() || B.IsNearlyZero()) { return true; }
				const double Dot = FVector::DotProduct(A, B);
				return Settings->bInvertAngularThreshold ? (Dot < DotThreshold) : (Dot > DotThreshold);
			};

			// Emits one simplified edge FromNode -> ToLink.Node, recording the original edges it absorbs
			// (PendingEdges + ToLink.Edge) as UnionSize. If the pair already exists (no parallel edges),
			// the accounting is merged into the existing entry rather than overwritten.
			auto EmitEdge = [&](const int32 FromNode, const PCGExGraphs::FLink& ToLink)
			{
				PendingEdges.Add(ToLink.Edge);

				PCGExGraphs::FEdge OutEdge = PCGExGraphs::FEdge{};
				const bool bNewEdge = GraphBuilder->Graph->InsertEdge(Cluster->GetNodePointIndex(FromNode), Cluster->GetNodePointIndex(ToLink.Node), OutEdge, IOIndex);

				PCGExGraphs::FGraphEdgeMetadata& EdgeMetadata = GraphBuilder->Graph->GetOrCreateEdgeMetadata(OutEdge.Index);

				if (bNewEdge)
				{
					EdgeMetadata.UnionSize = PendingEdges.Num();
					EdgesUnion->NewEntryAt_Unsafe(OutEdge.Index)->Add_Unsafe(IOIndex, PendingEdges);
				}
				else if (const TSharedPtr<PCGExData::IUnionData> Existing = EdgesUnion->Get(OutEdge.Index))
				{
					// Pair already exists (not expected for valid chains): merge so nothing is lost.
					EdgeMetadata.UnionSize += PendingEdges.Num();
					Existing->Add(IOIndex, PendingEdges);
				}
				else
				{
					EdgeMetadata.UnionSize = PendingEdges.Num();
					EdgesUnion->NewEntryAt_Unsafe(OutEdge.Index)->Add(IOIndex, PendingEdges);
				}

				PendingEdges.Reset();
			};

			if (!Chain->bIsClosedLoop)
			{
				// Open chain: Seed and Links.Last() are fixed endpoints, always kept. Every interior node
				// Links[0..MaxIndex-1] is tested -- including Links[0], whose predecessor is the Seed.
				int32 LastKeptNode = Chain->Seed.Node;
				FVector LastKeptPos = Cluster->GetPos(LastKeptNode);

				for (int32 i = 0; i < MaxIndex; ++i)
				{
					const PCGExGraphs::FLink Cur = Links[i];
					const int32 PrevNode = (i == 0) ? Chain->Seed.Node : Links[i - 1].Node;
					const FVector CurPos = Cluster->GetPos(Cur.Node);

					bool bSkip = IsCollinear(PrevNode, Cur.Node, Links[i + 1].Node);
					if (!bSkip && FuseDistance > 0)
					{
						bSkip = FVector::DistSquared(LastKeptPos, CurPos) <= FuseDistance;
					}

					if (bSkip)
					{
						PendingEdges.Add(Cur.Edge);
						continue;
					}

					EmitEdge(LastKeptNode, Cur);
					LastKeptNode = Cur.Node;
					LastKeptPos = CurPos;
				}

				// Terminal endpoint is always kept.
				EmitEdge(LastKeptNode, Links.Last());
			}
			else
			{
				// Closed loop: every node (including Seed) is binary and collapsible. Model a ring of
				// Links.Num()+1 nodes; RingLinkAt(r).Edge arrives at node r from its predecessor.
				const int32 RingSize = Links.Num() + 1;

				auto RingNodeAt = [&](const int32 r) -> int32 { return r == 0 ? Chain->Seed.Node : Links[r - 1].Node; };
				auto RingLinkAt = [&](const int32 r) -> PCGExGraphs::FLink { return r == 0 ? Chain->Seed : Links[r - 1]; };

				// Flag every angular corner once, reusing the verdict for anchor selection and the walk;
				// the first corner is a stable anchor.
				TArray<bool> bCorner;
				bCorner.SetNumUninitialized(RingSize);
				int32 Anchor = 0;
				bool bAnyCorner = false;
				for (int32 r = 0; r < RingSize; ++r)
				{
					bCorner[r] = !IsCollinear(RingNodeAt((r - 1 + RingSize) % RingSize), RingNodeAt(r), RingNodeAt((r + 1) % RingSize));
					if (bCorner[r] && !bAnyCorner) { Anchor = r; bAnyCorner = true; }
				}

				// Collect kept ring indices: corners that survive the fuse test, walked from the anchor.
				TArray<int32> Kept;
				Kept.Reserve(RingSize);
				if (bAnyCorner)
				{
					Kept.Add(Anchor);
					FVector LastKeptPos = Cluster->GetPos(RingNodeAt(Anchor));
					for (int32 k = 1; k < RingSize; ++k)
					{
						const int32 r = (Anchor + k) % RingSize;
						if (!bCorner[r]) { continue; }
						const FVector CurPos = Cluster->GetPos(RingNodeAt(r));
						if (FuseDistance > 0 && FVector::DistSquared(LastKeptPos, CurPos) <= FuseDistance) { continue; }
						Kept.Add(r);
						LastKeptPos = CurPos;
					}

					// Apply fuse across the closing wrap too (the anchor was exempt above), but never below 3 nodes.
					if (FuseDistance > 0 && Kept.Num() > 3 &&
						FVector::DistSquared(Cluster->GetPos(RingNodeAt(Kept.Last())), Cluster->GetPos(RingNodeAt(Anchor))) <= FuseDistance)
					{
						Kept.Pop();
					}
				}

				// A simple graph can't represent a cycle under 3 nodes; pad with spread-out ring nodes
				// (RingSize >= 3 guarantees enough exist) to keep the loop valid.
				if (Kept.Num() < 3)
				{
					auto TryAdd = [&](const int32 r) { if (Kept.Num() < 3 && !Kept.Contains(r)) { Kept.Add(r); } };
					TryAdd(0);
					TryAdd(RingSize / 3);
					TryAdd((2 * RingSize) / 3);
					for (int32 r = 0; r < RingSize && Kept.Num() < 3; ++r) { TryAdd(r); }
				}

				// Emit one edge per consecutive kept pair (cyclic), absorbing the skipped edges in each gap.
				Kept.Sort();
				for (int32 i = 0; i < Kept.Num(); ++i)
				{
					const int32 FromR = Kept[i];
					const int32 ToR = Kept[(i + 1) % Kept.Num()];
					for (int32 r = (FromR + 1) % RingSize; r != ToR; r = (r + 1) % RingSize) { PendingEdges.Add(RingLinkAt(r).Edge); }
					EmitEdge(RingNodeAt(FromR), RingLinkAt(ToR));
				}
			}
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExSimplifyClustersContext, UPCGExSimplifyClustersSettings>::Cleanup();
		ProcessedChains.Empty();
	}

	const PCGExGraphs::FGraphMetadataDetails* FBatch::GetGraphMetadataDetails()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(SimplifyClusters)
		GraphMetadataDetails.Update(Context, Settings->EdgeUnionData);
		GraphMetadataDetails.EdgesBlendingDetailsPtr = &Settings->EdgeBlendingDetails;
		GraphMetadataDetails.EdgesCarryOverDetails = &Context->EdgeCarryOverDetails;
		return &GraphMetadataDetails;
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(SimplifyClusters)
		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->FilterFactories, FacadePreloader);
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(SimplifyClusters)

		const TSharedPtr<PCGExData::FUnionMetadata> SparseEdgesUnion = MakeShared<PCGExData::FUnionMetadata>();
		SparseEdgesUnion->SetNum(PCGExData::PCGExPointIO::GetTotalPointsNum(Edges));
		GraphBuilder->Graph->EdgesUnion = SparseEdgesUnion;

		// Pre-size edge metadata so the locked GetOrCreateEdgeMetadata never needs to auto-grow
		GraphBuilder->Graph->ReserveForEdges(PCGExData::PCGExPointIO::GetTotalPointsNum(Edges));

		const int32 NumPoints = VtxDataFacade->GetNum();
		Breakpoints = MakeShared<TArray<int8>>();

		if (!Context->FilterFactories.IsEmpty())
		{
			Breakpoints->Init(false, NumPoints);

			// Process breakpoint filters
			PCGEX_MAKE_SHARED(FilterManager, PCGExPointFilter::FManager, VtxDataFacade)
			TArray<int8>& Breaks = *Breakpoints;
			if (FilterManager->Init(ExecutionContext, Context->FilterFactories))
			{
				for (int i = 0; i < NumPoints; i++)
				{
					Breaks[i] = FilterManager->Test(i);
				}
			}
		}
		else
		{
			if (Context->EdgeFilterFactories.IsEmpty())
			{
				Breakpoints->Init(false, NumPoints);
			}
			else
			{
				Breakpoints->Init(Settings->EdgeFilterRole == EPCGExSimplifyClusterEdgeFilterRole::Collapse, NumPoints);
			}
		}

		TBatch<FProcessor>::Process();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		PCGEX_TYPED_PROCESSOR
		TypedProcessor->Breakpoints = Breakpoints;
		return TBatch<FProcessor>::PrepareSingle(InProcessor);
	}
}


#undef LOCTEXT_NAMESPACE
