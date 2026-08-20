// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Core/PCGExTensorSampler.h"

#include "PCGExTensorSamplerRK4.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, DisplayName = "RK4", meta=(DisplayName = "RK4", ToolTip ="Samples the field using Runge-Kutta 4 method", PCGExNodeLibraryDoc="tensors/samplers/tensor-sampler-rk4"))
class UPCGExTensorSamplerRK4 : public UPCGExTensorSampler
{
	GENERATED_BODY()

public:
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;
	virtual bool PrepareForData(FPCGExContext* InContext) override;
	virtual PCGExTensor::FTensorSample Sample(const TArray<TSharedPtr<PCGExTensorOperation>>& InTensors, int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const override;
};
