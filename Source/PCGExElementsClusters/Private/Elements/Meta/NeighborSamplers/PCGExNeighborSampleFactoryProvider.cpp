// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleFactoryProvider.h"

#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"


#define LOCTEXT_NAMESPACE "PCGExCreateNeighborSample"
#define PCGEX_NAMESPACE PCGExCreateNeighborSample

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoNeighborSampler, UPCGExNeighborSamplerFactoryData)

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
		ValueFilters->Init(InContext, ValueFilterFactories);
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
	// Do pre-allocation here
}

void FPCGExNeighborSampleOperation::ProcessNode(const int32 NodeIndex, const PCGExMT::FScope& Scope)
{
	const PCGExClusters::FNode& Node = (*Cluster->Nodes)[NodeIndex];

	if (PointFilters && !PointFilters->Test(Node))
	{
		return;
	}

	int32 CurrentDepth = 0;
	int32 Count = 0;
	double TotalWeight = 0;

	const TUniquePtr<TArray<PCGExGraphs::FLink>> A = MakeUnique<TArray<PCGExGraphs::FLink>>();
	const TUniquePtr<TArray<PCGExGraphs::FLink>> B = MakeUnique<TArray<PCGExGraphs::FLink>>();

	TArray<PCGExGraphs::FLink>* CurrentNeighbors = A.Get();
	TArray<PCGExGraphs::FLink>* NextNeighbors = B.Get();
	TSet<int32> VisitedNodes;

	VisitedNodes.Add(NodeIndex);
	CurrentNeighbors->Append(Node.Links);

	PrepareNode(Node, Scope);
	const FVector Origin = Cluster->GetPos(Node);

	const int32 SafeMaxDepth = FMath::Min(1, SamplingConfig.MaxDepth);

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
				LocalWeight = SamplingConfig.BlendOver == EPCGExBlendOver::Index ? 1 - (CurrentDepth / SafeMaxDepth) : SamplingConfig.FixedBlend;
			}

			LocalWeight = WeightLUT->Eval(LocalWeight);

			if (SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx)
			{
				SampleNeighborNode(Node, Lk, LocalWeight, Scope);
			}
			else
			{
				SampleNeighborEdge(Node, Lk, LocalWeight, Scope);
			}

			Count++;
			TotalWeight += LocalWeight;
		}

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
					if (!ValueFilters->Results[Cluster->GetNodePointIndex(Next)])
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
}

#if WITH_EDITOR
FString UPCGExNeighborSampleProviderSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

TSharedPtr<FPCGExNeighborSampleOperation> UPCGExNeighborSamplerFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NeighborSampleOperation)
	PCGEX_SAMPLER_CREATE_OPERATION
	return NewOperation;
}

void UPCGExNeighborSamplerFactoryData::RegisterVtxBuffersDependencies(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (!VtxFilterFactories.IsEmpty())
	{
		for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Filter : VtxFilterFactories)
		{
			Filter->RegisterBuffersDependencies(InContext, FacadePreloader);
		}
	}

	if (!ValueFilterFactories.IsEmpty())
	{
		for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Filter : ValueFilterFactories)
		{
			Filter->RegisterBuffersDependencies(InContext, FacadePreloader);
		}
	}
}

void UPCGExNeighborSamplerFactoryData::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const
{
	Super::RegisterAssetDependencies(InDependencies);
	InDependencies.Add(SamplingConfig.WeightCurve.ToSoftObjectPath());
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

	PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceVtxFiltersLabel, SamplerFactory->EdgesFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);

	PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceUseValueIfFilters, SamplerFactory->ValueFilterFactories, PCGExFactories::ClusterNodeFilters(), false);

	return Super::CreateFactory(InContext, SamplerFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
