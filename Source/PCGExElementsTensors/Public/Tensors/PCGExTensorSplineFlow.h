// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorOperation.h"
#include "Core/PCGExTensorSplineFactoryProvider.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExTensorSplineFlow.generated.h"


USTRUCT(BlueprintType)
struct FPCGExTensorSplineFlowConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorSplineFlowConfig()
		: FPCGExTensorConfigBase(false)
	{
	}

	/** Sample inputs.*/
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/**  Base radius of the spline. Will be scaled by control points' scale length */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Radius = 100;

	/**  Which spline transform axis is to be used */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExAxis SplineDirection = EPCGExAxis::Forward;
};

/**
 * 
 */
class FPCGExTensorSplineFlow : public PCGExTensorOperation
{
public:
	FPCGExTensorSplineFlowConfig Config;
	const TArray<FPCGSplineStruct>* Splines = nullptr;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorSplineFlowFactory : public UPCGExTensorSplineFactoryData
{
	GENERATED_BODY()

public:
	FPCGExTensorSplineFlowConfig Config;
	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-spline-flow"))
class UPCGExCreateTensorSplineFlowSettings : public UPCGExTensorSplineFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorSplineFlow, "Tensor : Spline Flow", "A tensor that represent a vector/flow field along a spline")

#endif
	//~End UPCGSettings

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorSplineFlowConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
