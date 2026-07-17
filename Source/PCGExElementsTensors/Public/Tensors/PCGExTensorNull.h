// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"


#include "PCGExTensorNull.generated.h"


USTRUCT(BlueprintType)
struct FPCGExTensorNullConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorNullConfig()
		: FPCGExTensorConfigBase(false, false)
	{
	}
};

/**
 * 
 */
class FPCGExTensorNull : public PCGExTensorPointOperation
{
public:
	FPCGExTensorNullConfig Config;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorNullFactory : public UPCGExTensorPointFactoryData
{
	GENERATED_BODY()

public:
	FPCGExTensorNullConfig Config;
	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-null"))
class UPCGExCreateTensorNullSettings : public UPCGExTensorPointFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorNull, "Tensor : Null", "A tensor that represent a Null field")

#endif
	//~End UPCGSettings

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorNullConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
