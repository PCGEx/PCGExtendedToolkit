// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertySpecialNeighbors.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"


#define LOCTEXT_NAMESPACE "PCGExVtxPropertySpecialNeighbors"
#define PCGEX_NAMESPACE PCGExVtxPropertySpecialNeighbors

bool FPCGExVtxPropertySpecialNeighbors::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!FPCGExVtxPropertyOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!Config.LargestNeighbor.Validate(InContext) || !Config.SmallestNeighbor.Validate(InContext))
	{
		bIsValidOperation = false;
		return false;
	}

	Config.LargestNeighbor.Init(InVtxDataFacade.ToSharedRef());
	Config.SmallestNeighbor.Init(InVtxDataFacade.ToSharedRef());

	return bIsValidOperation;
}

void FPCGExVtxPropertySpecialNeighbors::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
	int32 LLargest = MIN_int32;
	int32 ILargest = -1;

	int32 LSmallest = MAX_int32;
	int32 ISmallest = -1;

	for (int i = 0; i < Adjacency.Num(); i++)
	{
		const PCGExClusters::FAdjacencyData& A = Adjacency[i];
		const int32 NumAdj = Cluster->GetNode(A.NodeIndex)->Num();

		if (NumAdj > LLargest)
		{
			ILargest = i;
			LLargest = NumAdj;
		}

		if (NumAdj < LSmallest)
		{
			ISmallest = i;
			LSmallest = NumAdj;
		}
	}

	if (ILargest != -1)
	{
		Config.LargestNeighbor.Set(Node.PointIndex, Adjacency[ILargest], LLargest);
	}
	else
	{
		Config.LargestNeighbor.Set(Node.PointIndex, 0, FVector::ZeroVector, -1, -1, 0);
	}

	if (ISmallest != -1)
	{
		Config.SmallestNeighbor.Set(Node.PointIndex, Adjacency[ISmallest], LSmallest);
	}
	else
	{
		Config.SmallestNeighbor.Set(Node.PointIndex, 0, FVector::ZeroVector, -1, -1, 0);
	}
}

#if WITH_EDITOR
FString UPCGExVtxPropertySpecialNeighborsSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertySpecialNeighborsFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertySpecialNeighbors)
	PCGEX_VTX_EXTRA_CREATE
	return NewOperation;
}

UPCGExFactoryData* UPCGExVtxPropertySpecialNeighborsSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExVtxPropertySpecialNeighborsFactory* NewFactory = InContext->ManagedObjects->New<UPCGExVtxPropertySpecialNeighborsFactory>();
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
