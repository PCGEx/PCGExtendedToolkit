// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorOperation.h"
#include "Core/PCGExTensorSplineFactoryProvider.h"
#include "PCGExTensorSplinePole.generated.h"


USTRUCT(BlueprintType)
struct FPCGExTensorSplinePoleConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorSplinePoleConfig()
		: FPCGExTensorConfigBase(false)
	{
	}

	/** Sample inputs.*/
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/**  Base radius of the spline. Will be scaled by control points' scale length */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Radius = 100;
};

/**
 * 
 */
class FPCGExTensorSplinePole : public PCGExTensorOperation
{
public:
	FPCGExTensorSplinePoleConfig Config;
	const TArray<FPCGSplineStruct>* Splines = nullptr;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorSplinePoleFactory : public UPCGExTensorSplineFactoryData
{
	GENERATED_BODY()

public:
	FPCGExTensorSplinePoleConfig Config;
	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-spline-pole"))
class UPCGExCreateTensorSplinePoleSettings : public UPCGExTensorSplineFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorSplinePole, "Tensor : Spline Pole", "A tensor that represent a vector/flow field along a spline")

#endif
	//~End UPCGSettings

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorSplinePoleConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
