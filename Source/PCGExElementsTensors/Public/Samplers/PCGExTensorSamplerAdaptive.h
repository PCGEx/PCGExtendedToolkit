// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensorSampler.h"
#include "PCGExTensorSamplerAdaptive.generated.h"

UCLASS(MinimalAPI, DisplayName = "Adaptive RK", meta=(DisplayName = "Adaptive RK", ToolTip = "Adaptive step size based on field curvature. More accurate in curved regions.", PCGExNodeLibraryDoc="tensors/samplers/tensor-sampler-adaptive-rk"))
class UPCGExTensorSamplerAdaptive : public UPCGExTensorSampler
{
	GENERATED_BODY()

public:
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;
	virtual bool PrepareForData(FPCGExContext* InContext) override;
	virtual PCGExTensor::FTensorSample Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const override;

protected:
	double EstimateCurvature(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, int32 InSeedIndex, const FTransform& InProbe, double StepSize) const;
};
