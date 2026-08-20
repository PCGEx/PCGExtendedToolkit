// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Factories/PCGExInstancedFactory.h"

#include "PCGExTensorSampler.generated.h"

class PCGExTensorOperation;
/**
 * 
 */
UCLASS(DisplayName = "Default", meta=(DisplayName = "Default", ToolTip ="Samples a single location in the tensor field.", PCGExNodeLibraryDoc="tensors/samplers/tensor-sampler-default"))
class PCGEXELEMENTSTENSORS_API UPCGExTensorSampler : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Radius = 1;

	/** Minimum step size as fraction of base radius */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.01, ClampMax=1.0))
	double MinStepFraction = 0.1;

	/** Maximum step size as fraction of base radius */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.1, ClampMax=2.0))
	double MaxStepFraction = 1.0;

	/** Error tolerance for step size adaptation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.001, ClampMax=0.5))
	double ErrorTolerance = 0.01;

	/** Maximum sub-steps per sample */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1, ClampMax=16))
	int32 MaxSubSteps = 4;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;
	virtual bool PrepareForData(FPCGExContext* InContext);
	virtual PCGExTensor::FTensorSample RawSample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, int32 InSeedIndex, const FTransform& InProbe) const;
	virtual PCGExTensor::FTensorSample Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const;
};
