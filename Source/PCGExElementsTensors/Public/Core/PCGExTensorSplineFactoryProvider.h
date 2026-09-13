// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTensorFactoryProvider.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineStruct.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Filters/Points/PCGExPolyPathFilterFactory.h"

#include "PCGExTensorSplineFactoryProvider.generated.h"

class PCGExTensorOperation;

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSTENSORS_API UPCGExTensorSplineFactoryData : public UPCGExTensorFactoryData
{
	GENERATED_BODY()

public:
	EPCGExSplinePointTypeRedux PointType = EPCGExSplinePointTypeRedux::Linear;
	bool bSmoothLinear = true;
	bool bBuildFromPaths = false;

protected:
	TArray<TSharedPtr<const FPCGSplineStruct>> ManagedSplines;
	TArray<FPCGSplineStruct> Splines;

	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult InitInternalData(FPCGExContext* InContext) override;
	virtual bool InitInternalFacade(FPCGExContext* InContext);

	virtual void BeginDestroy() override;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExProxyInterface))
class PCGEXELEMENTSTENSORS_API UPCGExTensorSplineFactoryProviderSettings : public UPCGExTensorFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	virtual bool GetBuildFromPoints() const
	{
		return false;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
