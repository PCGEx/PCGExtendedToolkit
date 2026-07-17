// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorSpin.h"
#include "PCGExVersion.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorSpin"
#define PCGEX_NAMESPACE CreateTensorSpin

namespace PCGExTensor
{
	bool FSpinEffectorsArray::Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory)
	{
		const UPCGExTensorSpinFactory* SpinFactory = Cast<UPCGExTensorSpinFactory>(InFactory);
		Config = SpinFactory->Config;

		if (Config.AxisInput == EPCGExInputValueType::Attribute)
		{
			AxisBuffer = InFactory->InputDataFacade->GetBroadcaster<FVector>(Config.AxisAttribute);
			if (!AxisBuffer)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Axis, Config.AxisAttribute)
				return false;
			}
		}

		if (!FEffectorsArray::Init(InContext, InFactory))
		{
			return false;
		}

		AxisBuffer.Reset();

		return true;
	}

	void FSpinEffectorsArray::PrepareSinglePoint(const int32 Index, const FTransform& InTransform, FPackedEffector& OutPackedEffector)
	{
		// Force forward-facing transform
		// As that's the direction we use during tensor sampling
		// This is so tensor transform is "cached" into the point at the time of tensor creation
		// instead of recomputing this every time the tensor is used

		// Sampling tensors is already rather expensive as-is

		if (AxisBuffer)
		{
			if (Config.AxisTransform == EPCGExTransformMode::Absolute)
			{
				Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, AxisBuffer->Read(Index));
			}
			else
			{
				Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, InTransform.TransformVectorNoScale(AxisBuffer->Read(Index)));
			}
		}
		else if (Config.AxisConstant != EPCGExAxis::Forward)
		{
			Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, PCGExMath::GetDirection(Rotations[Index], Config.AxisConstant));
		}
	}
}

bool FPCGExTensorSpin::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorPointOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorSpin::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(InPosition, FVector::One());

	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	auto ProcessNeighbor = [&](const PCGExOctree::FItem& InItem)
	{
		PCGExTensor::FEffectorMetrics Metrics;
		if (const auto* E = ComputeFactor(InPosition, InItem.Index, Metrics))
		{
			Samples.Emplace_GetRef(
				FVector::CrossProduct((E->Location - InPosition).GetSafeNormal(), Effectors->GetRotation(InItem.Index).RotateVector(Metrics.Guide)).GetSafeNormal(),
				E->Potency * PotencyFalloffLUT->Eval(Metrics.Factor),
				E->Weight * WeightFalloffLUT->Eval(Metrics.Factor));
		}
	};

	Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGEX_TENSOR_BOILERPLATE(Spin, {}, {})

#if WITH_EDITOR
void UPCGExCreateTensorSpinSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorSpinSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
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
