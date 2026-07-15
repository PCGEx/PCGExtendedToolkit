// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorSplineFlow.h"
#include "PCGExVersion.h"

PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineStruct.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Containers/PCGExManagedObjects.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorSplineFlow"
#define PCGEX_NAMESPACE CreateTensorSplineFlow

bool FPCGExTensorSplineFlow::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorSplineFlow::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	for (const FPCGSplineStruct& Spline : *Splines)
	{
		FTransform T = FTransform::Identity;
		PCGExTensor::FEffectorMetrics Metrics;

		if (!ComputeFactor(InPosition, Spline, Config.Radius, T, Metrics))
		{
			continue;
		}

		Samples.Emplace_GetRef(FRotationMatrix::MakeFromX(PCGExMath::GetDirection(T.GetRotation(), Config.SplineDirection)).ToQuat().RotateVector(Metrics.Guide), Metrics.Potency, Metrics.Weight);
	}

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGExFactories::EPreparationResult UPCGExTensorSplineFlowFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	SampleInputs = Config.SampleInputs;
	return Super::Prepare(InContext, TaskManager);
}

PCGEX_TENSOR_BOILERPLATE(
	SplineFlow,
	{
	NewFactory->Config.PotencyValue.Constant *= NewFactory->Config.PotencyScale;
	},
	{
	NewOperation->Splines = &Splines;
	})

#if WITH_EDITOR
void UPCGExCreateTensorSplineFlowSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorSplineFlowSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
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
