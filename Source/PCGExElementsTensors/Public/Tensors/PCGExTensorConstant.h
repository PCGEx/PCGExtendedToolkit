// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"

#include "PCGExTensorConstant.generated.h"

USTRUCT(BlueprintType)
struct FPCGExTensorConstantConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorConstantConfig()
		: FPCGExTensorConfigBase()
	{
	}

	/** The constant direction vector for this tensor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector Direction = FVector::ForwardVector;
};

/**
 * 
 */
class FPCGExTensorConstant : public PCGExTensorOperation
{
public:
	FPCGExTensorConstantConfig Config;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorConstantFactory : public UPCGExTensorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTensorConstantConfig Config;

	UPROPERTY()
	FVector Constant = FVector::OneVector;

	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;

protected:
	virtual PCGExFactories::EPreparationResult InitInternalData(FPCGExContext* InContext) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-constant"))
class UPCGExCreateTensorConstantSettings : public UPCGExTensorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TensorConstant, "Tensor : Constant", "A tensor that has a constant value in the field. Note that this tensor will prevent sampling from failing.")

#endif
	//~End UPCGSettings

	/** Base weight multiplier for this tensor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double TensorWeight = 1;

	/** The constant direction vector for this tensor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector Direction = FVector::ForwardVector;

	/** Base potency multiplier for this tensor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Potency = 1;

	/** Tensor mutations settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Sampling Mutations"))
	FPCGExTensorSamplingMutationsDetails Mutations;

	/** Tensor properties */
	UPROPERTY(meta=(PCG_NotOverridable, HideInDetailPanel))
	FPCGExTensorConstantConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
