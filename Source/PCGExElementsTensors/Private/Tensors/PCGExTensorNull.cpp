// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorNull.h"
#include "PCGExVersion.h"

#include "Containers/PCGExManagedObjects.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorNull"
#define PCGEX_NAMESPACE CreateTensorNull

bool FPCGExTensorNull::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorPointOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorNull::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(InPosition, FVector::One());

	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
	{
		PCGExTensor::FEffectorMetrics Metrics;
		if (!ComputeFactor(InPosition, InEffector.Index, Metrics))
		{
			return;
		}
		Samples.Emplace_GetRef(FVector::ZeroVector, 1, 1);
	};

	Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	return Samples.Flatten(Samples.TotalPotency * Config.TensorWeight);
}

PCGEX_TENSOR_BOILERPLATE(Null, {}, {})

#if WITH_EDITOR
void UPCGExCreateTensorNullSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorNullSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
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
