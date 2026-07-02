// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleBlend.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExBlendOpsManager.h"


#define LOCTEXT_NAMESPACE "PCGExCreateNeighborSample"
#define PCGEX_NAMESPACE PCGExCreateNeighborSample

void FPCGExNeighborSampleBlend::PrepareForCluster(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster> InCluster, const TSharedRef<PCGExData::FFacade> InVtxDataFacade, const TSharedRef<PCGExData::FFacade> InEdgeDataFacade)
{
	FPCGExNeighborSampleOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade);

	bIsValidOperation = false;

	BlendOpsManager = MakeShared<PCGExBlending::FBlendOpsManager>();

	BlendOpsManager->SetWeightFacade(VtxDataFacade);
	BlendOpsManager->SetTargetFacade(VtxDataFacade);
	BlendOpsManager->SetSources(Factory->SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx ? VtxDataFacade : EdgeDataFacade);

	if (!BlendOpsManager->Init(Context, Factory->BlendingFactories))
	{
		return;
	}

	bIsValidOperation = true;
}

void FPCGExNeighborSampleBlend::PrepareForLoops(const TArray<PCGExMT::FScope>& Loops)
{
	FPCGExNeighborSampleOperation::PrepareForLoops(Loops);
	BlendOpsManager->InitScopedTrackers(Loops);
}

void FPCGExNeighborSampleBlend::PrepareNode(const PCGExClusters::FNode& TargetNode, const PCGExMT::FScope& Scope) const
{
	BlendOpsManager->BeginMultiBlend(TargetNode.PointIndex, BlendOpsManager->GetScopedTrackers(Scope));
}

void FPCGExNeighborSampleBlend::SampleNeighborNode(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
	BlendOpsManager->MultiBlend(Cluster->GetNodePointIndex(Lk), TargetNode.PointIndex, Weight, BlendOpsManager->GetScopedTrackers(Scope));
}

void FPCGExNeighborSampleBlend::SampleNeighborEdge(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope)
{
	BlendOpsManager->MultiBlend(Cluster->GetNodePointIndex(Lk), TargetNode.PointIndex, Weight, BlendOpsManager->GetScopedTrackers(Scope));
}

void FPCGExNeighborSampleBlend::FinalizeNode(const PCGExClusters::FNode& TargetNode, const int32 Count, const double TotalWeight, const PCGExMT::FScope& Scope)
{
	BlendOpsManager->EndMultiBlend(TargetNode.PointIndex, BlendOpsManager->GetScopedTrackers(Scope));
}

void FPCGExNeighborSampleBlend::CompleteOperation()
{
	FPCGExNeighborSampleOperation::CompleteOperation();
	BlendOpsManager.Reset();
}

#if WITH_EDITOR
FString UPCGExNeighborSampleBlendSettings::GetDisplayName() const
{
	return TEXT("TBD");
}
#endif

TSharedPtr<FPCGExNeighborSampleOperation> UPCGExNeighborSamplerFactoryBlend::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NeighborSampleBlend)
	PCGEX_SAMPLER_CREATE_OPERATION

	NewOperation->Factory = this;

	return NewOperation;
}

bool UPCGExNeighborSamplerFactoryBlend::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	if (!Super::RegisterConsumableAttributes(InContext))
	{
		return false;
	}
	// TODO !
	return true;
}

void UPCGExNeighborSamplerFactoryBlend::RegisterVtxBuffersDependencies(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterVtxBuffersDependencies(InContext, InVtxDataFacade, FacadePreloader);

	if (SamplingConfig.NeighborSource == EPCGExClusterElement::Vtx)
	{
		PCGExBlending::RegisterBuffersDependencies_Sources(InContext, FacadePreloader, BlendingFactories);
	}
}

TArray<FPCGPinProperties> UPCGExNeighborSampleBlendSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Required);
	return PinProperties;
}

UPCGExFactoryData* UPCGExNeighborSampleBlendSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNeighborSamplerFactoryBlend* SamplerFactory = InContext->ManagedObjects->New<UPCGExNeighborSamplerFactoryBlend>();

	if (!PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(InContext, PCGExBlending::Labels::SourceBlendingLabel, SamplerFactory->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}))
	{
		InContext->ManagedObjects->Destroy(SamplerFactory);
		return nullptr;
	}

	return Super::CreateFactory(InContext, SamplerFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
