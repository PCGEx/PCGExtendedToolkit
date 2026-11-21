// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleFactoryProvider.h"

#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExLink.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"


#define LOCTEXT_NAMESPACE "PCGExCreateNeighborSample"
#define PCGEX_NAMESPACE PCGExCreateNeighborSample

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoNeighborSampler, UPCGExNeighborSamplerFactoryData)

namespace PCGExNeighborSample
{
	struct FTraversalScratch
	{
		TArray<PCGExGraphs::FLink> Current;
		TArray<PCGExGraphs::FLink> Next;
		TSet<int32> Visited;
	};
}

void FPCGExSamplingConfig::Init()
{
	WeightLUT = WeightCurveLookup.MakeLookup(bUseLocalCurve, LocalWeightCurve, WeightCurve);
}

void FPCGExNeighborSampleOperation::PrepareForCluster(FPCGExContext* InContext, TSharedRef<PCGExClusters::FCluster> InCluster, TSharedRef<PCGExData::FFacade> InVtxDataFacade, TSharedRef<PCGExData::FFacade> InEdgeDataFacade)
{
	Cluster = InCluster;

	VtxDataFacade = InVtxDataFacade;
	EdgeDataFacade = InEdgeDataFacade;

	if (!VtxFilterFactories.IsEmpty())
	{
		PointFilters = MakeShared<PCGExClusterFilter::FManager>(InCluster, InVtxDataFacade, InEdgeDataFacade);
		PointFilters->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
		PointFilters->Init(InContext, VtxFilterFactories);
	}

	if (!ValueFilterFactories.IsEmpty())
	{
		ValueFilters = MakeShared<PCGExClusterFilter::FManager>(InCluster, InVtxDataFacade, InEdgeDataFacade);
		ValueFilters->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
		ValueFilters->bCacheResults = true;
		if (!ValueFilters->Init(InContext, ValueFilterFactories))
		{
			ValueFilters.Reset();
		}
	}
}

bool FPCGExNeighborSampleOperation::IsOperationValid()
{
	return bIsValidOperation;
}

TSharedRef<PCGExData::FPointIO> FPCGExNeighborSampleOperation::GetSourceIO() const
{
	return GetSourceDataFacade()->Source;
}

TSharedRef<PCGExData::FFacade> FPCGExNeighborSampleOperation::GetSourceDataFacade() const
{
	return SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx ? VtxDataFacade.ToSharedRef() : EdgeDataFacade.ToSharedRef();
}

void FPCGExNeighborSampleOperation::PrepareForLoops(const TArray<PCGExMT::FScope>& Loops)
{
	if (SamplingConfig.MaxDepth > 1)
	{
		TraversalScratch = MakeShared<PCGExMT::TScopedPtr<PCGExNeighborSample::FTraversalScratch>>(Loops);
	}
}

void FPCGExNeighborSampleOperation::ProcessNode(const int32 NodeIndex, const PCGExMT::FScope& Scope)
{
	const PCGExClusters::FNode& Node = (*Cluster->Nodes)[NodeIndex];

	if (PointFilters && !PointFilters->Test(Node))
	{
		return;
	}

	int32 Count = 0;
	double TotalWeight = 0;

	PrepareNode(Node, Scope);
	const FVector Origin = Cluster->GetPos(Node);

	const int32 SafeMaxDepth = FMath::Max(1, SamplingConfig.MaxDepth);

	if (SamplingConfig.MaxDepth <= 1)
	{
		// Direct neighbors only: no frontier to gather, nothing to mark visited.
		SampleLinks(Node, Origin, Node.Links, 1, SafeMaxDepth, Scope, Count, TotalWeight);
		FinalizeNode(Node, Count, TotalWeight, Scope);
		return;
	}

	check(TraversalScratch);
	PCGExNeighborSample::FTraversalScratch& Scratch = TraversalScratch->Get_Ref(Scope);

	TArray<PCGExGraphs::FLink>* CurrentNeighbors = &Scratch.Current;
	TArray<PCGExGraphs::FLink>* NextNeighbors = &Scratch.Next;
	TSet<int32>& VisitedNodes = Scratch.Visited;

	CurrentNeighbors->Reset();
	VisitedNodes.Reset();

	VisitedNodes.Add(NodeIndex);
	CurrentNeighbors->Append(Node.Links);

	int32 CurrentDepth = 0;

	while (CurrentDepth <= SafeMaxDepth)
	{
		if (CurrentNeighbors->IsEmpty())
		{
			break;
		}
		CurrentDepth++;

		for (const PCGExGraphs::FLink Lk : (*CurrentNeighbors))
		{
			VisitedNodes.Add(Lk.Node);
		}

		SampleLinks(Node, Origin, *CurrentNeighbors, CurrentDepth, SafeMaxDepth, Scope, Count, TotalWeight);

		if (CurrentDepth >= SamplingConfig.MaxDepth)
		{
			break;
		}

		// Gather next depth

		NextNeighbors->Reset();
		for (const PCGExGraphs::FLink& Old : (*CurrentNeighbors))
		{
			const PCGExGraphs::NodeLinks& Neighbors = Cluster->GetNode(Old.Node)->Links;
			if (ValueFilters)
			{
				for (const PCGExGraphs::FLink Next : Neighbors)
				{
					int32 NextIndex = Next.Node;
					if (VisitedNodes.Contains(NextIndex))
					{
						continue;
					}
					if (!ValueFilters->Results[NextIndex])
					{
						VisitedNodes.Add(NextIndex);
						continue;
					}
					NextNeighbors->Add(Next);
				}
			}
			else
			{
				for (const PCGExGraphs::FLink Next : Neighbors)
				{
					if (VisitedNodes.Contains(Next.Node))
					{
						continue;
					}
					NextNeighbors->Add(Next);
				}
			}
		}

		std::swap(CurrentNeighbors, NextNeighbors);
	}

	FinalizeNode(Node, Count, TotalWeight, Scope);
}

void FPCGExNeighborSampleOperation::SampleLinks(const PCGExClusters::FNode& TargetNode, const FVector& Origin, const TConstArrayView<PCGExGraphs::FLink> Links, const int32 Depth, const int32 SafeMaxDepth, const PCGExMT::FScope& Scope, int32& Count, double& TotalWeight)
{
	for (const PCGExGraphs::FLink Lk : Links)
	{
		double LocalWeight;

		if (SamplingConfig.BlendOver == EPCGExBlendOver::Distance)
		{
			const double Dist = FVector::Dist(Origin, Cluster->GetPos(Lk)); // Use Neighbor.FromNode to accumulate per-path distance
			if (Dist > SamplingConfig.MaxDistance)
			{
				continue;
			}
			LocalWeight = 1 - (Dist / SamplingConfig.MaxDistance);
		}
		else
		{
			LocalWeight = SamplingConfig.BlendOver == EPCGExBlendOver::Index ? 1.0 - (static_cast<double>(Depth) / SafeMaxDepth) : SamplingConfig.FixedBlend;
		}

		LocalWeight = WeightLUT->Eval(LocalWeight);

		if (SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx)
		{
			SampleNeighborNode(TargetNode, Lk, LocalWeight, Scope);
		}
		else
		{
			SampleNeighborEdge(TargetNode, Lk, LocalWeight, Scope);
		}

		Count++;
		TotalWeight += LocalWeight;
	}
}

void FPCGExNeighborSampleOperation::PrepareNode(const PCGExClusters::FNode& TargetNode, const PCGExMT::FScope& Scope) const
{
}

void FPCGExNeighborSampleOperation::SampleNeighborNode(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
}

void FPCGExNeighborSampleOperation::SampleNeighborEdge(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
}

void FPCGExNeighborSampleOperation::FinalizeNode(const PCGExClusters::FNode& TargetNode, const int32 Count, const double TotalWeight, const PCGExMT::FScope& Scope)
{
}

void FPCGExNeighborSampleOperation::CompleteOperation()
{
	TraversalScratch.Reset();
}

TSharedPtr<FPCGExNeighborSampleOperation> UPCGExNeighborSamplerFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NeighborSampleOperation)
	PCGEX_SAMPLER_CREATE_OPERATION
	return NewOperation;
}

void UPCGExNeighborSamplerFactoryData::RegisterVtxBuffersDependencies(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, PCGExData::FFacadePreloader& FacadePreloader) const
{
	PCGExPointFilter::RegisterBuffersDependencies(InContext, VtxFilterFactories, FacadePreloader);
	PCGExPointFilter::RegisterBuffersDependencies(InContext, ValueFilterFactories, FacadePreloader);
}

TArray<FPCGPinProperties> UPCGExNeighborSampleProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	bool bIsRequired = false;
	if (SupportsVtxFilters(bIsRequired))
	{
		if (bIsRequired)
		{
			PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceVtxFiltersLabel, "Filters applied to vtx", Required)
		}
		else
		{
			PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceVtxFiltersLabel, "Filters applied to vtx", Advanced)
		}
	}
	if (SupportsEdgeFilters(bIsRequired))
	{
		if (bIsRequired)
		{
			PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceEdgeFiltersLabel, "Filters applied to edges", Required)
		}
		else
		{
			PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceEdgeFiltersLabel, "Filters applied to edges", Advanced)
		}
	}
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceUseValueIfFilters, "Filters used to check if a node can be used as a value source or not.", Advanced)
	return PinProperties;
}

bool UPCGExNeighborSampleProviderSettings::SupportsVtxFilters(bool& bIsRequired) const
{
	bIsRequired = false;
	return true;
}

bool UPCGExNeighborSampleProviderSettings::SupportsEdgeFilters(bool& bIsRequired) const
{
	bIsRequired = false;
	return false;
}

UPCGExFactoryData* UPCGExNeighborSampleProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNeighborSamplerFactoryData* SamplerFactory = Cast<UPCGExNeighborSamplerFactoryData>(InFactory);

	SamplerFactory->Priority = Priority;
	SamplerFactory->SamplingConfig = SamplingConfig;
	SamplerFactory->SamplingConfig.Init();

	PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceVtxFiltersLabel, SamplerFactory->VtxFilterFactories, PCGExFactories::ClusterNodeFilters(), false);

	PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceEdgeFiltersLabel, SamplerFactory->EdgesFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);

	PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceUseValueIfFilters, SamplerFactory->ValueFilterFactories, PCGExFactories::ClusterNodeFilters(), false);

	return Super::CreateFactory(InContext, SamplerFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
