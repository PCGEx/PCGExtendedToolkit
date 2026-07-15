// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorNoiseBounded.h"
#include "PCGExVersion.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExNoiseGenerator.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorNoiseBounded"
#define PCGEX_NAMESPACE CreateTensorNoiseBounded

bool FPCGExTensorNoiseBounded::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorPointOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorNoiseBounded::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	const FVector& InPosition = InProbe.GetLocation();
	const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(InPosition, FVector::One());

	const bool bNrmNoise = Config.bNormalizeNoiseSampling;

	FVector Noise = NoiseGenerator->GetVector(InPosition);
	if (Config.bNormalizeNoiseSampling)
	{
		Noise.Normalize();
	}

	PCGExTensor::FEffectorMetrics Metrics;
	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	if (NoiseMaskGenerator)
	{
		auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
		{
			if (const double Mask = NoiseMaskGenerator->GetDouble(InPosition);
				!FMath::IsNearlyZero(Mask))
			{
				if (const auto* E = ComputeFactor(InPosition, InEffector.Index, Metrics))
				{
					Samples.Emplace_GetRef(Noise, Metrics.Potency * Mask, Metrics.Weight * Mask);
				}
			}
		};

		Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	}
	else
	{
		auto ProcessNeighbor = [&](const PCGExOctree::FItem& InEffector)
		{
			if (const auto* E = ComputeFactor(InPosition, InEffector.Index, Metrics))
			{
				Samples.Emplace_GetRef(Noise, Metrics.Potency, Metrics.Weight);
			}
		};

		Effectors->GetOctree()->FindElementsWithBoundsTest(BCAE, ProcessNeighbor);
	}

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

namespace PCGExTensor
{
	bool FNoiseBoundedEffectorsArray::Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory)
	{
		const UPCGExTensorNoiseBoundedFactory* NoiseBoundedFactory = Cast<UPCGExTensorNoiseBoundedFactory>(InFactory);
		Config = NoiseBoundedFactory->Config;
		if (!FEffectorsArray::Init(InContext, InFactory))
		{
			return false;
		}
		return true;
	}
}

PCGEX_TENSOR_BOILERPLATE(
	NoiseBounded,
	{
	NewFactory->NoiseGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!NewFactory->NoiseGenerator->Init(InContext)) { return nullptr; }
	NewFactory->NoiseMaskGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!NewFactory->NoiseMaskGenerator->Init(InContext, PCGExNoise3D::Labels::SourceNoise3DMaskLabel, false)) { NewFactory->NoiseMaskGenerator = nullptr; }
	},
	{
	NewOperation->NoiseGenerator = NoiseGenerator;
	NewOperation->NoiseMaskGenerator = NoiseMaskGenerator;
	})

#if WITH_EDITOR
void UPCGExCreateTensorNoiseBoundedSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorNoiseBoundedSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TSharedPtr<PCGExTensor::FEffectorsArray> UPCGExTensorNoiseBoundedFactory::GetEffectorsArray() const
{
	return MakeShared<PCGExTensor::FNoiseBoundedEffectorsArray>();
}

TArray<FPCGPinProperties> UPCGExCreateTensorNoiseBoundedSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DLabel, "Noise nodes", Required, FPCGExDataTypeInfoNoise3D::AsId())
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DMaskLabel, "Additional layer of noise used as influence over the first. Optional.", Normal, FPCGExDataTypeInfoNoise3D::AsId())
	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
