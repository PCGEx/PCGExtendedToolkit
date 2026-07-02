// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleFilters.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExCreateNeighborSample"
#define PCGEX_NAMESPACE PCGExCreateNeighborSample

void FPCGExNeighborSampleFilters::PrepareForCluster(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster> InCluster, const TSharedRef<PCGExData::FFacade> InVtxDataFacade, const TSharedRef<PCGExData::FFacade> InEdgeDataFacade)
{
	FPCGExNeighborSampleOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade);
	PointFilters.Reset();

	bIsValidOperation = false;

	FilterManager = MakeShared<PCGExClusterFilter::FManager>(InCluster, InVtxDataFacade, InEdgeDataFacade);

	const int32 NumNodes = Cluster->Nodes->Num();
	Inside.Init(0, NumNodes);
	InsideWeight.Init(0, NumNodes);
	Outside.Init(0, NumNodes);
	OutsideWeight.Init(0, NumNodes);

	if (Config.bWriteInsideNum)
	{
		if (!Config.bNormalizeInsideNum)
		{
			NumInsideBuffer = VtxDataFacade->GetWritable<int32>(Config.InsideNumAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			NormalizedNumInsideBuffer = VtxDataFacade->GetWritable<double>(Config.InsideNumAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteOutsideNum)
	{
		if (!Config.bNormalizeInsideNum)
		{
			NumOutsideBuffer = VtxDataFacade->GetWritable<int32>(Config.OutsideNumAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			NormalizedNumOutsideBuffer = VtxDataFacade->GetWritable<double>(Config.OutsideNumAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteTotalNum)
	{
		TotalNumBuffer = VtxDataFacade->GetWritable<int32>(Config.TotalNumAttributeName, 0, true, PCGExData::EBufferInit::New);
	}

	if (Config.bWriteInsideWeight)
	{
		if (!Config.bNormalizeInsideWeight)
		{
			WeightInsideBuffer = VtxDataFacade->GetWritable<double>(Config.InsideWeightAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			NormalizedWeightInsideBuffer = VtxDataFacade->GetWritable<double>(Config.InsideWeightAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteOutsideWeight)
	{
		if (!Config.bNormalizeOutsideWeight)
		{
			WeightOutsideBuffer = VtxDataFacade->GetWritable<double>(Config.OutsideWeightAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			NormalizedWeightOutsideBuffer = VtxDataFacade->GetWritable<double>(Config.OutsideWeightAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteTotalWeight)
	{
		TotalWeightBuffer = VtxDataFacade->GetWritable<int32>(Config.TotalWeightAttributeName, 0, true, PCGExData::EBufferInit::New);
	}

	if (SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx)
	{
		FilterManager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
		if (!FilterManager->Init(InContext, VtxFilterFactories))
		{
			return;
		}
	}
	else
	{
		FilterManager->bUseEdgeAsPrimary = true;
		FilterManager->SetSupportedTypes(&PCGExFactories::ClusterEdgeFilters());
		if (!FilterManager->Init(InContext, EdgesFilterFactories))
		{
			return;
		}
	}

	bIsValidOperation = true;
}

void FPCGExNeighborSampleFilters::PrepareNode(const PCGExClusters::FNode& TargetNode, const PCGExMT::FScope& Scope) const
{
	FPCGExNeighborSampleOperation::PrepareNode(TargetNode, Scope);
}

void FPCGExNeighborSampleFilters::SampleNeighborNode(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
	if (FilterManager->Test(*Cluster->GetNode(Lk)))
	{
		Inside[TargetNode.Index] += 1;
		InsideWeight[TargetNode.Index] += Weight;
	}
	else
	{
		Outside[TargetNode.Index] += 1;
		OutsideWeight[TargetNode.Index] += Weight;
	}
}

void FPCGExNeighborSampleFilters::SampleNeighborEdge(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
	if (FilterManager->Test(*Cluster->GetEdge(Lk)))
	{
		Inside[TargetNode.Index] += 1;
		InsideWeight[TargetNode.Index] += Weight;
	}
	else
	{
		Outside[TargetNode.Index] += 1;
		OutsideWeight[TargetNode.Index] += Weight;
	}
}

void FPCGExNeighborSampleFilters::FinalizeNode(const PCGExClusters::FNode& TargetNode, const int32 Count, const double TotalWeight, const PCGExMT::FScope& Scope)
{
	const int32 WriteIndex = TargetNode.PointIndex;
	const int32 ReadIndex = TargetNode.Index;

	if (NumInsideBuffer)
	{
		NumInsideBuffer->SetValue(WriteIndex, Inside[ReadIndex]);
	}
	else if (NormalizedNumInsideBuffer)
	{
		NormalizedNumInsideBuffer->SetValue(WriteIndex, static_cast<double>(Inside[ReadIndex]) / static_cast<double>(Count));
	}

	if (NumOutsideBuffer)
	{
		NumOutsideBuffer->SetValue(WriteIndex, Outside[ReadIndex]);
	}
	else if (NormalizedNumOutsideBuffer)
	{
		NormalizedNumOutsideBuffer->SetValue(WriteIndex, static_cast<double>(Outside[ReadIndex]) / static_cast<double>(Count));
	}

	if (TotalNumBuffer)
	{
		TotalNumBuffer->SetValue(WriteIndex, Count);
	}

	if (WeightInsideBuffer)
	{
		WeightInsideBuffer->SetValue(WriteIndex, InsideWeight[ReadIndex]);
	}
	else if (NormalizedWeightInsideBuffer)
	{
		NormalizedWeightInsideBuffer->SetValue(WriteIndex, InsideWeight[ReadIndex] / TotalWeight);
	}

	if (WeightOutsideBuffer)
	{
		WeightOutsideBuffer->SetValue(WriteIndex, Outside[ReadIndex]);
	}
	else if (NormalizedWeightOutsideBuffer)
	{
		NormalizedWeightOutsideBuffer->SetValue(WriteIndex, OutsideWeight[ReadIndex] / TotalWeight);
	}

	if (TotalWeightBuffer)
	{
		TotalWeightBuffer->SetValue(WriteIndex, TotalWeight);
	}
}

void FPCGExNeighborSampleFilters::CompleteOperation()
{
	FPCGExNeighborSampleOperation::CompleteOperation();
	Inside.Empty();
	Outside.Empty();
	FilterManager.Reset();
}

TSharedPtr<FPCGExNeighborSampleOperation> UPCGExNeighborSamplerFactoryFilters::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NeighborSampleFilters)
	PCGEX_SAMPLER_CREATE_OPERATION
	NewOperation->Config = Config;
	return NewOperation;
}

UPCGExNeighborSampleFiltersSettings::UPCGExNeighborSampleFiltersSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SamplingConfig.bSupportsBlending = false;
}

bool UPCGExNeighborSampleFiltersSettings::SupportsVtxFilters(bool& bIsRequired) const
{
	bIsRequired = true;
	return SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx;
}

bool UPCGExNeighborSampleFiltersSettings::SupportsEdgeFilters(bool& bIsRequired) const
{
	bIsRequired = true;
	return SamplingConfig.NeighborSource == EPCGExClusterElement::Edge;
}

UPCGExFactoryData* UPCGExNeighborSampleFiltersSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	if (Config.bWriteInsideNum)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.InsideNumAttributeName, nullptr)
	}
	if (Config.bWriteOutsideNum)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.OutsideNumAttributeName, nullptr)
	}
	if (Config.bWriteTotalNum)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.TotalNumAttributeName, nullptr)
	}
	if (Config.bWriteInsideWeight)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.InsideWeightAttributeName, nullptr)
	}
	if (Config.bWriteOutsideWeight)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.OutsideWeightAttributeName, nullptr)
	}
	if (Config.bWriteTotalWeight)
	{
		PCGEX_VALIDATE_NAME_C_RET(InContext, Config.TotalWeightAttributeName, nullptr)
	}

	UPCGExNeighborSamplerFactoryFilters* SamplerFactory = InContext->ManagedObjects->New<UPCGExNeighborSamplerFactoryFilters>();
	SamplerFactory->Config = Config;

	return Super::CreateFactory(InContext, SamplerFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
