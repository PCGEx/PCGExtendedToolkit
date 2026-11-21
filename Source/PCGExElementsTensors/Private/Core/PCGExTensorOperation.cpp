// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExTensorOperation.h"

#include "Core/PCGExTensorFactoryProvider.h"

bool PCGExTensorOperation::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	Factory = InFactory;
	PotencyFalloffLUT = InFactory->BaseConfig.PotencyFalloffLUT;
	WeightFalloffLUT = InFactory->BaseConfig.WeightFalloffLUT;

	// Reads this operation's own BaseConfig, the one ComputeFactor samples.
	bConstantGuide = PCGExTensor::TryGetConstantValue(BaseConfig.LocalGuideCurve, GuideConstant);
	return true;
}

PCGExTensor::FTensorSample PCGExTensorOperation::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	return PCGExTensor::FTensorSample{};
}

bool PCGExTensorOperation::PrepareForData(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	PrimaryDataFacade = InDataFacade;
	return true;
}

bool PCGExTensorPointOperation::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}

	const UPCGExTensorPointFactoryData* PointFactoryData = Cast<UPCGExTensorPointFactoryData>(InFactory);
	Effectors = PointFactoryData->EffectorsArray;

	return true;
}
