// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"
#include "PCGExTensorNoiseBounded.generated.h"


namespace PCGExNoise3D
{
	class FNoiseGenerator;
}

USTRUCT(BlueprintType)
struct FPCGExTensorNoiseBoundedConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorNoiseBoundedConfig()
		: FPCGExTensorConfigBase()
	{
	}

	/** If enabled normalize the sampled noise direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bNormalizeNoiseSampling = true;
};

/**
 * 
 */
class FPCGExTensorNoiseBounded : public PCGExTensorPointOperation
{
public:
	FPCGExTensorNoiseBoundedConfig Config;
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator = nullptr;
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseMaskGenerator = nullptr;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};

namespace PCGExTensor
{
	class FNoiseBoundedEffectorsArray : public FEffectorsArray
	{
	public:
		FPCGExTensorNoiseBoundedConfig Config;
		TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator = nullptr;
		TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseMaskGenerator = nullptr;

		virtual bool Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory) override;
	};
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorNoiseBoundedFactory : public UPCGExTensorPointFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTensorNoiseBoundedConfig Config;

	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator = nullptr;
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseMaskGenerator = nullptr;

	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;

protected:
	virtual TSharedPtr<PCGExTensor::FEffectorsArray> GetEffectorsArray() const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-noise-bounded"))
class UPCGExCreateTensorNoiseBoundedSettings : public UPCGExTensorPointFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorNoiseBounded, "Tensor : Noise (Bounded)", "A tensor that uses 3D noises as direction, within effector bounds")

#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorNoiseBoundedConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
