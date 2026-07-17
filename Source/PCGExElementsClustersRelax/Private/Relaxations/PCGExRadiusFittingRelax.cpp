// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Relaxations/PCGExRadiusFittingRelax.h"

#pragma region UPCGExRadiusFittingRelax

#if WITH_EDITOR
void UPCGExRadiusFittingRelax::ApplyShorthandDeprecation()
{
	RadiusValue.Update(RadiusInput_DEPRECATED, RadiusAttribute_DEPRECATED, Radius_DEPRECATED);
}
#endif

void UPCGExRadiusFittingRelax::RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterPrimaryBuffersDependencies(InContext, FacadePreloader);
	if (RadiusValue.Input == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, RadiusValue.Attribute);
	}
}

bool UPCGExRadiusFittingRelax::PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster)
{
	if (!Super::PrepareForCluster(InContext, InCluster))
	{
		return false;
	}

	RadiusBuffer = RadiusValue.GetValueSetting();
	if (!RadiusBuffer->Init(PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

void UPCGExRadiusFittingRelax::Step2(const PCGExClusters::FNode& Node)
{
	const FVector& CurrentPos = (ReadBuffer->GetData() + Node.Index)->GetLocation();
	const double& CurrentRadius = RadiusBuffer->Read(Node.PointIndex);

	// Apply repulsion forces between all pairs of nodes

	for (int32 OtherNodeIndex = Node.Index + 1; OtherNodeIndex < Cluster->Nodes->Num(); OtherNodeIndex++)
	{
		const PCGExClusters::FNode* OtherNode = Cluster->GetNode(OtherNodeIndex);
		const FVector& OtherPos = (ReadBuffer->GetData() + OtherNodeIndex)->GetLocation();

		FVector Delta = OtherPos - CurrentPos;
		const double Distance = Delta.Size();
		const double Overlap = (CurrentRadius + RadiusBuffer->Read(OtherNode->PointIndex)) - Distance;

		if (Overlap <= 0 || Distance <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		AddDelta(OtherNode->Index, Node.Index, (RepulsionConstant * (Overlap / FMath::Square(Distance)) * (Delta / Distance)));
	}
}

#pragma endregion
