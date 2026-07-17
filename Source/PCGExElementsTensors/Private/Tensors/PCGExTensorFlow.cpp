// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorFlow.h"
#include "PCGExVersion.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorFlow"
#define PCGEX_NAMESPACE CreateTensorFlow

bool FPCGExTensorFlow::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorPointOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorFlow::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(InPosition, FVector::One());

	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
	{
		PCGExTensor::FEffectorMetrics Metrics;
		if (const auto* E = ComputeFactor(InPosition, InEffector.Index, Metrics))
		{
			Samples.Emplace_GetRef(Effectors->GetRotation(InEffector.Index).RotateVector(Metrics.Guide), Metrics.Potency, Metrics.Weight);
		}
	};

	Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

namespace PCGExTensor
{
	bool FFlowEffectorsArray::Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory)
	{
		const UPCGExTensorFlowFactory* FlowFactory = Cast<UPCGExTensorFlowFactory>(InFactory);
		Config = FlowFactory->Config;

		if (Config.DirectionInput == EPCGExInputValueType::Attribute)
		{
			DirectionBuffer = FlowFactory->InputDataFacade->GetBroadcaster<FVector>(Config.DirectionAttribute);
			if (!DirectionBuffer)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Direction, Config.DirectionAttribute)
				return false;
			}

			DirectionMultiplier = Config.bInvertDirection ? -1 : 1;
		}

		if (!FEffectorsArray::Init(InContext, InFactory))
		{
			return false;
		}

		DirectionBuffer.Reset();

		return true;
	}

	void FFlowEffectorsArray::PrepareSinglePoint(const int32 Index, const FTransform& InTransform, FPackedEffector& OutPackedEffector)
	{
		// Force forward-facing transform
		// As that's the direction we use during tensor sampling
		// This is so tensor transform is "cached" into the point at the time of tensor creation
		// instead of recomputing this every time the tensor is used

		// Sampling tensors is already rather expensive as-is

		if (DirectionBuffer)
		{
			if (Config.DirectionTransform == EPCGExTransformMode::Absolute)
			{
				Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, DirectionBuffer->Read(Index) * DirectionMultiplier);
			}
			else
			{
				Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, InTransform.TransformVectorNoScale(DirectionBuffer->Read(Index) * DirectionMultiplier));
			}
		}
		else if (Config.DirectionConstant != EPCGExAxis::Forward)
		{
			Rotations[Index] = PCGExMath::MakeDirection(EPCGExAxis::Forward, PCGExMath::GetDirection(Rotations[Index], Config.DirectionConstant));
		}
	}
}


PCGEX_TENSOR_BOILERPLATE(Flow, {}, {})

#if WITH_EDITOR
void UPCGExCreateTensorFlowSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorFlowSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TSharedPtr<PCGExTensor::FEffectorsArray> UPCGExTensorFlowFactory::GetEffectorsArray() const
{
	return MakeShared<PCGExTensor::FFlowEffectorsArray>();
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
