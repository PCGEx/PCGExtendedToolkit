// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTensorSplineFlow.h"
#include "Core/PCGExTensor.h"

#include "PCGExTensorPathPole.generated.h"


USTRUCT(BlueprintType)
struct FPCGExTensorPathPoleConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorPathPoleConfig()
		: FPCGExTensorConfigBase(false)
	{
	}

	/** Which point type to use. Shared amongst all points; if you want tight control, create a fully-fledged spline instead. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplinePointTypeRedux PointType = EPCGExSplinePointTypeRedux::Linear;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Smooth Linear", EditCondition="PointType == EPCGExSplinePointTypeRedux::Linear", EditConditionHides))
	bool bSmoothLinear = true;

	/** Sample inputs.*/
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/**  Base radius of the spline. Will be scaled by control points' scale length */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayAfter="TensorWeight"))
	double Radius = 100;
};

/**
 * 
 */
class FPCGExTensorPathPole : public PCGExTensorOperation
{
public:
	FPCGExTensorPathPoleConfig Config;
	const TArray<TSharedPtr<const FPCGSplineStruct>>* Splines = nullptr;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorPathPoleFactory : public UPCGExTensorSplineFlowFactory
{
	GENERATED_BODY()

public:
	FPCGExTensorPathPoleConfig Config;
	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/tensors/tensor-path-pole"))
class UPCGExCreateTensorPathPoleSettings : public UPCGExTensorSplineFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorPathPole, "Tensor : Path Pole", "A tensor that represent a vector/flow field along a path")

#endif
	//~End UPCGSettings

	virtual bool GetBuildFromPoints() const override
	{
		return true;
	}

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorPathPoleConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
