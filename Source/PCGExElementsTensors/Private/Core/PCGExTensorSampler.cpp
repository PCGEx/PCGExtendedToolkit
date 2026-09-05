// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Core/PCGExTensorSampler.h"

#include "Core/PCGExTensorOperation.h"


void UPCGExTensorSampler::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}

bool UPCGExTensorSampler::PrepareForData(FPCGExContext* InContext)
{
	return true;
}

PCGExTensor::FTensorSample UPCGExTensorSampler::RawSample(
	const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors,
	const int32 InSeedIndex,
	const FTransform& InProbe) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTensorSampler::RawSample);

	// First pass: collect samples and total weight
	TArray<PCGExTensor::FTensorSample, TInlineAllocator<8>> Samples;
	Samples.Reserve(InTensors.Num());
	double TotalWeight = 0;

	for (const TSharedPtr<PCGExTensorOperation>& Op : InTensors)
	{
		PCGExTensor::FTensorSample Sample = Op->Sample(InSeedIndex, InProbe);
		if (Sample.Effectors == 0)
		{
			continue;
		}
		TotalWeight += Sample.Weight;
		Samples.Emplace(MoveTemp(Sample));
	}

	if (Samples.IsEmpty())
	{
		return PCGExTensor::FTensorSample();
	}

	// Early out for single sample (common case)
	if (Samples.Num() == 1)
	{
		return Samples[0];
	}

	// Every contributor at Weight 0: average uniformly rather than divide by zero.
	if (TotalWeight <= UE_SMALL_NUMBER)
	{
		for (PCGExTensor::FTensorSample& S : Samples) { S.Weight = 1; }
		TotalWeight = Samples.Num();
	}

	// Single pass weighted accumulation
	const double InvTotalWeight = 1.0 / TotalWeight;
	FVector WeightedDirection = FVector::ZeroVector;
	FQuat WeightedRotation = Samples[0].Rotation;
	double CumulativeWeight = Samples[0].Weight * InvTotalWeight;
	int32 TotalEffectors = Samples[0].Effectors;

	WeightedDirection = Samples[0].DirectionAndSize * CumulativeWeight;

	for (int32 i = 1; i < Samples.Num(); i++)
	{
		const PCGExTensor::FTensorSample& Sample = Samples[i];
		const double W = Sample.Weight * InvTotalWeight;

		WeightedDirection += Sample.DirectionAndSize * W;
		TotalEffectors += Sample.Effectors;

		// Incremental slerp
		const double NewCumulative = CumulativeWeight + W;
		WeightedRotation = FQuat::Slerp(WeightedRotation, Sample.Rotation, W / NewCumulative);
		CumulativeWeight = NewCumulative;
	}

	return PCGExTensor::FTensorSample(
		WeightedDirection,
		WeightedRotation.GetNormalized(),
		TotalEffectors,
		TotalWeight);
}

PCGExTensor::FTensorSample UPCGExTensorSampler::Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, const int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTensorSampler::Sample);

	const PCGExTensor::FTensorSample Result = RawSample(InTensors, InSeedIndex, InProbe);
	OutSuccess = Result.Effectors > 0;
	return Result;
}
