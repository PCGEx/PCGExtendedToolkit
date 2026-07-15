// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorInertia.h"
#include "PCGExVersion.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorInertia"
#define PCGEX_NAMESPACE CreateTensorInertia

bool FPCGExTensorInertia::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorPointOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorInertia::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(InPosition, FVector::One());

	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	if (Config.bSetInertiaOnce)
	{
		auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
		{
			PCGExTensor::FEffectorMetrics Metrics;
			if (!ComputeFactor(InPosition, InEffector.Index, Metrics))
			{
				return;
			}

			Samples.Emplace_GetRef(PCGExMath::GetDirection(PrimaryDataFacade->GetIn()->GetTransform(InSeedIndex).GetRotation() * FRotationMatrix::MakeFromX(Metrics.Guide).ToQuat(), Config.Axis), Metrics.Potency, Metrics.Weight);
		};

		Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	}
	else
	{
		auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
		{
			PCGExTensor::FEffectorMetrics Metrics;
			if (!ComputeFactor(InPosition, InEffector.Index, Metrics))
			{
				return;
			}

			Samples.Emplace_GetRef(PCGExMath::GetDirection(InProbe.GetRotation() * FRotationMatrix::MakeFromX(Metrics.Guide).ToQuat(), Config.Axis), Metrics.Potency, Metrics.Weight);
		};

		Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	}


	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGEX_TENSOR_BOILERPLATE(Inertia, {}, {})

#if WITH_EDITOR
void UPCGExCreateTensorInertiaSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorInertiaSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
