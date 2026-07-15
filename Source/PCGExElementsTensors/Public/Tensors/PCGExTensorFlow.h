// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"
#include "Data/PCGExDataHelpers.h"
#include "Math/PCGExMathAxis.h"
#include "PCGExTensorFlow.generated.h"


USTRUCT(BlueprintType)
struct FPCGExTensorFlowConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorFlowConfig()
		: FPCGExTensorConfigBase()
	{
		DirectionAttribute.Update(TEXT("$Rotation.Forward"));
	}

	/** Direction type.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExInputValueType DirectionInput = EPCGExInputValueType::Constant;

	/** Fetch the direction from a local attribute.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Direction (Attr)", EditCondition="DirectionInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector DirectionAttribute;

	/** Flip the direction vector (multiply by -1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Invert", EditCondition="DirectionInput != EPCGExInputValueType::Constant", EditConditionHides))
	bool bInvertDirection = false;

	/** Direction axis, read from the input points' transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Direction", EditCondition="DirectionInput == EPCGExInputValueType::Constant", EditConditionHides))
	EPCGExAxis DirectionConstant = EPCGExAxis::Forward;

	/** Whether the direction is absolute or should be transformed by the owner' transform .*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="DirectionInput != EPCGExInputValueType::Constant", EditConditionHides))
	EPCGExTransformMode DirectionTransform = EPCGExTransformMode::Relative;
};

/**
 * 
 */
class FPCGExTensorFlow : public PCGExTensorPointOperation
{
public:
	FPCGExTensorFlowConfig Config;
	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};

namespace PCGExTensor
{
	class FFlowEffectorsArray : public FEffectorsArray
	{
		FPCGExTensorFlowConfig Config;
		TSharedPtr<PCGExData::TBuffer<FVector>> DirectionBuffer;
		double DirectionMultiplier = 1;

	public:
		virtual bool Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory) override;

	protected:
		virtual void PrepareSinglePoint(const int32 Index, const FTransform& InTransform, FPackedEffector& OutPackedEffector) override;
	};
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorFlowFactory : public UPCGExTensorPointFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTensorFlowConfig Config;

	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;

protected:
	virtual TSharedPtr<PCGExTensor::FEffectorsArray> GetEffectorsArray() const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-flow"))
class UPCGExCreateTensorFlowSettings : public UPCGExTensorPointFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorFlow, "Tensor : Flow", "A tensor that represent a vector/flow field")

#endif
	//~End UPCGSettings

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorFlowConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
