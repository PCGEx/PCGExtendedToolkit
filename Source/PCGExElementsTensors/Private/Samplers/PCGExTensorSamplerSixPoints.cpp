// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Samplers/PCGExTensorSamplerSixPoints.h"

void UPCGExTensorSamplerSixPoints::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}

bool UPCGExTensorSamplerSixPoints::PrepareForData(FPCGExContext* InContext)
{
	return true;
}

PCGExTensor::FTensorSample UPCGExTensorSamplerSixPoints::Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, const int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTensorSamplerSixPoints::Sample);

	PCGExTensor::FTensorSample Result = PCGExTensor::FTensorSample();
	FQuat AvgRotation = FQuat::Identity;

	for (int i = 0; i < 6; i++)
	{
		FTransform PointProbe = InProbe;
		PointProbe.AddToTranslation(Points[i] * Radius);
		const PCGExTensor::FTensorSample Sample = Super::RawSample(InTensors, InSeedIndex, PointProbe);
		// Equal-weight incremental slerp -- += composes quats, which does not average orientations.
		AvgRotation = FQuat::Slerp(AvgRotation, Sample.Rotation, 1.0 / (i + 1));
		Result += Sample;
	}

	Result /= 6;
	Result.Rotation = AvgRotation.GetNormalized();
	OutSuccess = Result.Effectors > 0;

	return Result;
}
