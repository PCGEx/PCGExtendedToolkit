// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorNoise.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExNoise3DCommon.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"
#include "Helpers/PCGExNoiseGenerator.h"

#define LOCTEXT_NAMESPACE "PCGExCreateTensorNoise"
#define PCGEX_NAMESPACE CreateTensorNoise

bool FPCGExTensorNoise::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorNoise::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	const FVector InPosition = InProbe.GetLocation();

	FVector Noise = NoiseGenerator->GetVector(InPosition);
	if (Config.bNormalizeNoiseSampling)
	{
		Noise.Normalize();
	}

	if (NoiseMaskGenerator)
	{
		if (const double Mask = NoiseMaskGenerator->GetDouble(InPosition);
			!FMath::IsNearlyZero(Mask))
		{
			Samples.Emplace_GetRef(Noise, Config.PotencyValue.Constant * Mask, Config.WeightValue.Constant * Mask);
		}
	}
	else
	{
		Samples.Emplace_GetRef(Noise, Config.PotencyValue.Constant, Config.WeightValue.Constant);
	}

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGEX_TENSOR_BOILERPLATE(
	Noise,
	{
	NewFactory->Config.Mutations = Mutations;
	NewFactory->Config.PotencyValue.Constant = Potency;
	NewFactory->Config.PotencyValue.Input = EPCGExInputValueType::Constant;
	NewFactory->Config.WeightValue.Constant = 1;
	NewFactory->Config.TensorWeight = TensorWeight;
	NewFactory->Config.WeightValue.Input = EPCGExInputValueType::Constant;
	NewFactory->Config.bNormalizeNoiseSampling = bNormalizeNoiseSampling;
	NewFactory->NoiseGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!NewFactory->NoiseGenerator->Init(InContext)) { return nullptr; }
	NewFactory->NoiseMaskGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!NewFactory->NoiseMaskGenerator->Init(InContext, PCGExNoise3D::Labels::SourceNoise3DMaskLabel, false)) { NewFactory->NoiseMaskGenerator = nullptr; }
	},
	{
	NewOperation->NoiseGenerator = NoiseGenerator;
	NewOperation->NoiseMaskGenerator = NoiseMaskGenerator;
	})

PCGExFactories::EPreparationResult UPCGExTensorNoiseFactory::InitInternalData(FPCGExContext* InContext)
{
	if (Config.PotencyValue.Input == EPCGExInputValueType::Attribute)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attribute-driven Potency is not supported on Noise Tensor."));
		return PCGExFactories::EPreparationResult::Fail;
	}

	if (Config.WeightValue.Input == EPCGExInputValueType::Attribute)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attribute-driven Weight is not supported on Noise Tensor."));
		return PCGExFactories::EPreparationResult::Fail;
	}

	return Super::InitInternalData(InContext);
}

TArray<FPCGPinProperties> UPCGExCreateTensorNoiseSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DLabel, "Noise nodes", Required, FPCGExDataTypeInfoNoise3D::AsId())
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DMaskLabel, "Additional layer of noise used as influence over the first. Optional.", Normal, FPCGExDataTypeInfoNoise3D::AsId())
	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
