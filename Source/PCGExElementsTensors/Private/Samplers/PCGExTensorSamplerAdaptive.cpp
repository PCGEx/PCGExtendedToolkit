// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Samplers/PCGExTensorSamplerAdaptive.h"
#include "Core/PCGExTensorOperation.h"

void UPCGExTensorSamplerAdaptive::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExTensorSamplerAdaptive* TypedOther = Cast<UPCGExTensorSamplerAdaptive>(Other))
	{
		MinStepFraction = TypedOther->MinStepFraction;
		MaxStepFraction = TypedOther->MaxStepFraction;
		ErrorTolerance = TypedOther->ErrorTolerance;
		MaxSubSteps = TypedOther->MaxSubSteps;
	}
}

bool UPCGExTensorSamplerAdaptive::PrepareForData(FPCGExContext* InContext)
{
	return Super::PrepareForData(InContext);
}

double UPCGExTensorSamplerAdaptive::EstimateCurvature(
	const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors,
	int32 InSeedIndex, const FTransform& InProbe, double StepSize) const
{
	// Sample at current and slightly offset position to estimate direction change
	const PCGExTensor::FTensorSample S1 = Super::RawSample(InTensors, InSeedIndex, InProbe);
	if (S1.Effectors == 0)
	{
		return 0;
	}

	FTransform OffsetProbe = InProbe;
	OffsetProbe.AddToTranslation(S1.DirectionAndSize.GetSafeNormal() * StepSize * 0.5);

	const PCGExTensor::FTensorSample S2 = Super::RawSample(InTensors, InSeedIndex, OffsetProbe);
	if (S2.Effectors == 0)
	{
		return 0;
	}

	// Curvature approximation: angle change per unit distance
	const FVector D1 = S1.DirectionAndSize.GetSafeNormal();
	const FVector D2 = S2.DirectionAndSize.GetSafeNormal();

	const double DotProduct = FVector::DotProduct(D1, D2);
	const double Angle = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));

	return Angle / (StepSize * 0.5);
}

PCGExTensor::FTensorSample UPCGExTensorSamplerAdaptive::Sample(
	const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors,
	int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTensorSamplerAdaptive::Sample);

	// Start with a full step RK4
	const double BaseStep = Radius;
	const double MinStep = BaseStep * MinStepFraction;
	const double MaxStep = BaseStep * MaxStepFraction;

	const double Curvature = EstimateCurvature(InTensors, InSeedIndex, InProbe, BaseStep * 0.5);

	// Adapt step size based on curvature (higher curvature = smaller step)
	double AdaptedStep = BaseStep;
	if (Curvature > SMALL_NUMBER)
	{
		// Step size inversely proportional to curvature
		AdaptedStep = FMath::Clamp(ErrorTolerance / Curvature, MinStep, MaxStep);
	}

	// Determine number of sub-steps
	const int32 NumSubSteps = FMath::Clamp(FMath::CeilToInt(BaseStep / AdaptedStep), 1, MaxSubSteps);
	const double SubStepSize = BaseStep / NumSubSteps;

	// Accumulate result over sub-steps
	PCGExTensor::FTensorSample Result;
	FTransform CurrentProbe = InProbe;
	OutSuccess = false;

	for (int32 i = 0; i < NumSubSteps; i++)
	{
		// RK4 for this sub-step
		PCGExTensor::FTensorSample K1 = Super::RawSample(InTensors, InSeedIndex, CurrentProbe);
		if (K1.Effectors == 0)
		{
			break;
		}

		PCGExTensor::FTensorSample K2 = Super::RawSample(InTensors, InSeedIndex, K1.GetTransformed(CurrentProbe, SubStepSize * 0.5));
		PCGExTensor::FTensorSample K3 = Super::RawSample(InTensors, InSeedIndex, K2.GetTransformed(CurrentProbe, SubStepSize * 0.5));
		PCGExTensor::FTensorSample K4 = Super::RawSample(InTensors, InSeedIndex, K3.GetTransformed(CurrentProbe, SubStepSize));

		// Weighted RK4 combination for this sub-step
		const FVector SubStepDirection = (SubStepSize / 6.0) * (K1.DirectionAndSize + 2.0 * K2.DirectionAndSize + 2.0 * K3.DirectionAndSize + K4.DirectionAndSize);

		Result.DirectionAndSize += SubStepDirection;
		Result.Effectors += K1.Effectors;
		OutSuccess = true;

		// Advance probe for next sub-step
		CurrentProbe.AddToTranslation(SubStepDirection);
	}

	// Average rotation from final sample
	if (OutSuccess)
	{
		const PCGExTensor::FTensorSample FinalSample = Super::RawSample(InTensors, InSeedIndex, CurrentProbe);
		Result.Rotation = FinalSample.Rotation;
	}

	return Result;
}
