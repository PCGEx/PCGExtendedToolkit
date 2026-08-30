// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Samplers/PCGExTensorSamplerRK4.h"


void UPCGExTensorSamplerRK4::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}

bool UPCGExTensorSamplerRK4::PrepareForData(FPCGExContext* InContext)
{
	return true;
}

PCGExTensor::FTensorSample UPCGExTensorSamplerRK4::Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, const int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTensorSamplerRK4::Sample);

	PCGExTensor::FTensorSample Result = PCGExTensor::FTensorSample();
	OutSuccess = true;

	PCGExTensor::FTensorSample K1 = Super::RawSample(InTensors, InSeedIndex, InProbe);
	Result += K1;

	PCGExTensor::FTensorSample K2 = Super::RawSample(InTensors, InSeedIndex, K1.GetTransformed(InProbe, (Radius * 0.5)));
	Result += K2;

	PCGExTensor::FTensorSample K3 = Super::RawSample(InTensors, InSeedIndex, K2.GetTransformed(InProbe, (Radius * 0.5)));
	Result += K3;

	PCGExTensor::FTensorSample K4 = Super::RawSample(InTensors, InSeedIndex, K3.GetTransformed(InProbe, Radius));
	Result += K4;

	Result.DirectionAndSize = (Radius / 6.0f) * (K1.DirectionAndSize + 2.0f * K2.DirectionAndSize + 2.0f * K3.DirectionAndSize + K4.DirectionAndSize);

	// RK4-weighted (1,2,2,1) incremental slerp -- += composed the stage quats, which is not an average.
	FQuat Rotation = K1.Rotation;
	Rotation = FQuat::Slerp(Rotation, K2.Rotation, 2.0 / 3.0);
	Rotation = FQuat::Slerp(Rotation, K3.Rotation, 2.0 / 5.0);
	Rotation = FQuat::Slerp(Rotation, K4.Rotation, 1.0 / 6.0);
	Result.Rotation = Rotation.GetNormalized();

	OutSuccess = Result.Effectors > 0;

	return Result;
}
