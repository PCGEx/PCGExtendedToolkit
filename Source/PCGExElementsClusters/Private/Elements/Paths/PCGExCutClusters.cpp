// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Paths/PCGExCutClusters.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Math/PCGExMathBounds.h"
#include "Math/PCGExMathDistances.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExCutEdges"
#define PCGEX_NAMESPACE CutEdges

TArray<FPCGPinProperties> UPCGExCutEdgesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExPaths::Labels::SourcePathsLabel, "Cutting paths.", Required)
	if (Mode != EPCGExCutEdgesMode::Edges)
	{
		PCGEX_PIN_FILTERS(PCGExCutEdges::SourceNodeFilters, "Node preservation filters.", Normal)
	}
	if (Mode != EPCGExCutEdgesMode::Nodes)
	{
		PCGEX_PIN_FILTERS(PCGExCutEdges::SourceEdgeFilters, "Edge preservation filters.", Normal)
	}

	return PinProperties;
}

PCGExData::EIOInit UPCGExCutEdgesSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::New;
}

PCGExData::EIOInit UPCGExCutEdgesSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(CutEdges)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(CutEdges)

bool FPCGExCutEdgesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CutEdges)

	Context->bWantsVtxProcessing = Settings->Mode != EPCGExCutEdgesMode::Edges;
	Context->bWantsEdgesProcessing = Settings->Mode != EPCGExCutEdgesMode::Nodes;

	PCGEX_FWD(IntersectionDetails)
	Context->IntersectionDetails.Init();

	PCGEX_FWD(GraphBuilderDetails)

	if (Context->bWantsEdgesProcessing)
	{
		PCGExFactories::GetInputFactories(Context, PCGExCutEdges::SourceEdgeFilters, Context->EdgeFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);
	}

	if (Context->bWantsVtxProcessing)
	{
		PCGExFactories::GetInputFactories(Context, PCGExCutEdges::SourceNodeFilters, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters(), false);
	}

	PCGEX_MAKE_SHARED(PathCollection, PCGExData::FPointIOCollection, Context, PCGExPaths::Labels::SourcePathsLabel)
	if (PathCollection->IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Empty paths."));
		return false;
	}

	Context->PathFacades.Reserve(PathCollection->Num());
	Context->Paths.Reserve(PathCollection->Num());

	int32 ExcludedNum = 0;

	for (const TSharedPtr<PCGExData::FPointIO>& PathIO : PathCollection->Pairs)
	{
		if (PathIO->GetNum() < 2)
		{
			ExcludedNum++;
			continue;
		}

		PCGEX_MAKE_SHARED(Facade, PCGExData::FFacade, PathIO.ToSharedRef())
		Facade->bSupportsScopedGet = Context->bScopedAttributeGet;

		Context->PathFacades.Add(Facade.ToSharedRef());
	}

	if (ExcludedNum != 0)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Some input paths had less than 2 points and will be ignored."));
	}

	if (Context->PathFacades.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No valid paths found."));
		return false;
	}

	return true;
}

bool FPCGExCutEdgesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCutEdgesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CutEdges)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExPaths::Labels::State_BuildingPaths);
		PCGEX_ASYNC_GROUP_CHKD(Context->GetTaskManager(), BuildPathsTask)

		BuildPathsTask->OnSubLoopStartCallback = [Context](const PCGExMT::FScope& Scope)
		{
			const TSharedRef<PCGExData::FFacade> PathFacade = Context->PathFacades[Scope.Start];
			PCGEX_MAKE_SHARED(Path, PCGExPaths::FPath, PathFacade->GetIn(), 0)

			Path->BuildEdgeOctree();

			Context->Paths.Add(Path.ToSharedRef());
		};

		BuildPathsTask->StartSubLoops(Context->PathFacades.Num(), 1);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExPaths::Labels::State_BuildingPaths)
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				if (Context->bWantsVtxProcessing)
				{
					NewBatch->VtxFilterFactories = &Context->VtxFilterFactories;
				}
				if (Context->bWantsEdgesProcessing)
				{
					NewBatch->EdgeFilterFactories = &Context->EdgeFilterFactories;
				}
				NewBatch->GraphBuilderDetails = Context->GraphBuilderDetails;
			}))
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("Could not build any clusters."));
			return true;
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

namespace PCGExCutEdges
{
	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		// Create a light working copy with edges only, will be deleted.
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, Context->bWantsVtxProcessing, Context->bWantsEdgesProcessing, false);
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCutEdges::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->bInvert)
		{
			if (Context->bWantsEdgesProcessing)
			{
				for (PCGExGraphs::FEdge& E : *Cluster->Edges)
				{
					E.bValid = false;
				}
				StartParallelLoopForEdges();
			}

			if (Context->bWantsVtxProcessing)
			{
				for (PCGExClusters::FNode& N : *Cluster->Nodes)
				{
					N.bValid = false;
				}
				StartParallelLoopForNodes();
			}
		}
		else
		{
			if (Context->bWantsEdgesProcessing)
			{
				StartParallelLoopForEdges();
			}
			if (Context->bWantsVtxProcessing)
			{
				StartParallelLoopForNodes();
			}
		}

		return true;
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		EdgeDataFacade->Fetch(Scope);
		FilterEdgeScope(Scope);

		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;
		TConstPCGValueRange<FTransform> InVtxTransforms = VtxDataFacade->Source->GetIn()->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];

			if (EdgeFilterCache[Index])
			{
				if (Settings->bInvert)
				{
					Edge.bValid = true;
				}
				continue;
			}

			const FVector A1 = InVtxTransforms[Edge.Start].GetLocation();
			const FVector B1 = InVtxTransforms[Edge.End].GetLocation();
			const FVector Dir = (B1 - A1).GetSafeNormal();

			FBox EdgeBox = FBox(ForceInit);
			EdgeBox += A1;
			EdgeBox += B1;

			for (const TSharedRef<PCGExPaths::FPath>& Path : Context->Paths)
			{
				if (!Path->Bounds.Intersect(EdgeBox))
				{
					continue;
				}

				// Check paths
				Path->GetEdgeOctree()->FindFirstElementWithBoundsTest(EdgeBox, [&](const PCGExPaths::FPathEdge* PathEdge)
				{
					//if (Settings->bInvert) { if (Edge.bValid) { return false; } }
					//else if (!Edge.bValid) { return false; }

					if (Context->IntersectionDetails.bUseMinAngle || Context->IntersectionDetails.bUseMaxAngle)
					{
						if (!Context->IntersectionDetails.CheckDot(FMath::Abs(FVector::DotProduct(PathEdge->Dir, Dir))))
						{
							return true;
						}
					}

					const FVector A2 = Path->GetPos_Unsafe(PathEdge->Start);
					const FVector B2 = Path->GetPos_Unsafe(PathEdge->End);
					FVector A = FVector::ZeroVector;
					FVector B = FVector::ZeroVector;

					FMath::SegmentDistToSegment(A1, B1, A2, B2, A, B);
					//if (A == A1 || A == B1 || B == A2 || B == B2) { return true; }

					if (FVector::DistSquared(A, B) >= Context->IntersectionDetails.ToleranceSquared)
					{
						return true;
					}

					PCGExClusters::FNode* StartNode = Cluster->GetEdgeStart(Edge);
					PCGExClusters::FNode* EndNode = Cluster->GetEdgeEnd(Edge);

					if (Settings->bInvert)
					{
						FPlatformAtomics::InterlockedExchange(&Edge.bValid, 1);
						FPlatformAtomics::InterlockedExchange(&StartNode->bValid, 1);
						FPlatformAtomics::InterlockedExchange(&EndNode->bValid, 1);
					}
					else
					{
						FPlatformAtomics::InterlockedExchange(&Edge.bValid, 0);
						if (Settings->bAffectedEdgesAffectEndpoints)
						{
							FPlatformAtomics::InterlockedExchange(&StartNode->bValid, 0);
							FPlatformAtomics::InterlockedExchange(&EndNode->bValid, 0);
						}
					}

					return false;
				});

				if (Edge.bValid == static_cast<int8>(Settings->bInvert))
				{
				}
			}
		}
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		FilterVtxScope(Scope);

		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		const UPCGBasePointData* InVtxPointData = VtxDataFacade->GetIn();
		const PCGExMath::IDistances* Distances = PCGExMath::GetDistances(Settings->NodeDistanceSettings, Settings->NodeDistanceSettings);

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];

			if (IsNodePassingFilters(Node))
			{
				if (Settings->bInvert)
				{
					Node.bValid = true;
				}
				continue;
			}

			const PCGExData::FConstPoint NodePoint(InVtxPointData, Node.PointIndex);
			const FTransform& NodeTransform = NodePoint.GetTransform();
			const FVector A1 = NodeTransform.GetLocation();
			FBox PointBox = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::Bounds>(NodePoint).ExpandBy(Settings->NodeExpansion + Settings->IntersectionDetails.ToleranceSquared).TransformBy(NodeTransform);

			for (const TSharedRef<PCGExPaths::FPath>& Path : Context->Paths)
			{
				if (!Path->Bounds.Intersect(PointBox))
				{
					continue;
				}

				// Check paths
				Path->GetEdgeOctree()->FindFirstElementWithBoundsTest(PointBox, [&](const PCGExPaths::FPathEdge* PathEdge)
				{
					//if (Settings->bInvert) { if (Node.bValid) { return false; } }
					//else if (!Node.bValid) { return false; }

					const FVector A2 = Path->GetPos_Unsafe(PathEdge->Start);
					const FVector B2 = Path->GetPos_Unsafe(PathEdge->End);

					const FVector B1 = FMath::ClosestPointOnSegment(A1, A2, B2);
					const FVector C1 = Distances->GetSourceCenter(NodePoint, A1, B1);

					if (FVector::DistSquared(B1, C1) >= Context->IntersectionDetails.ToleranceSquared)
					{
						return true;
					}

					if (Settings->bInvert)
					{
						FPlatformAtomics::InterlockedExchange(&Node.bValid, 1);
						if (Settings->bAffectedNodesAffectConnectedEdges)
						{
							for (const PCGExGraphs::FLink Lk : Node.Links)
							{
								FPlatformAtomics::InterlockedExchange(&(Cluster->GetEdge(Lk))->bValid, 1);
								FPlatformAtomics::InterlockedExchange(&Cluster->GetNode(Lk)->bValid, 1);
							}
						}
					}
					else
					{
						FPlatformAtomics::InterlockedExchange(&Node.bValid, 0);
						if (Settings->bAffectedNodesAffectConnectedEdges)
						{
							for (const PCGExGraphs::FLink Lk : Node.Links)
							{
								FPlatformAtomics::InterlockedExchange(&(Cluster->GetEdge(Lk))->bValid, 0);
							}
						}
					}
					return false;
				});

				if (Node.bValid == static_cast<int8>(Settings->bInvert))
				{
				}
			}
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		FPlatformAtomics::InterlockedExchange(&EdgesProcessed, 1);
		TryConsolidate();
	}

	void FProcessor::OnNodesProcessingComplete()
	{
		FPlatformAtomics::InterlockedExchange(&NodesProcessed, 1);
		TryConsolidate();
	}

	void FProcessor::TryConsolidate()
	{
		switch (Settings->Mode)
		{
		case EPCGExCutEdgesMode::Nodes:
			if (!NodesProcessed)
			{
				return;
			}
			break;
		case EPCGExCutEdgesMode::Edges:
			if (!EdgesProcessed)
			{
				return;
			}
			break;
		case EPCGExCutEdgesMode::NodesAndEdges:
			if (!EdgesProcessed || !NodesProcessed)
			{
				return;
			}
			break;
		}

		if (Settings->bInvert && Settings->bKeepEdgesThatConnectValidNodes)
		{
			StartParallelLoopForRange(Cluster->Edges->Num());
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(Index);

			//if (Edge.bValid)		{			continue;		}

			const PCGExClusters::FNode* StartNode = Cluster->GetEdgeStart(Edge);
			const PCGExClusters::FNode* EndNode = Cluster->GetEdgeEnd(Edge);

			if (StartNode->bValid && EndNode->bValid)
			{
				Edge.bValid = true;
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		TArray<PCGExGraphs::FEdge> ValidEdges;
		Cluster->GetValidEdges(ValidEdges);

		if (ValidEdges.IsEmpty())
		{
			return;
		}

		GraphBuilder->Graph->InsertEdges(ValidEdges);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
