// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"
#include "PCGExClampDetails.generated.h"

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExClampDetails
{
	GENERATED_BODY()

	/** Enable minimum value clamping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bApplyClampMin = false;

	/** Values below this will be clamped up to this minimum. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bApplyClampMin"))
	double ClampMinValue = 0;

	/** Enable maximum value clamping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bApplyClampMax = false;

	/** Values above this will be clamped down to this maximum. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bApplyClampMax"))
	double ClampMaxValue = 0;

	FORCEINLINE double GetClampedValue(const double InValue) const
	{
		if (bApplyClampMin && InValue < ClampMinValue)
		{
			return ClampMinValue;
		}
		if (bApplyClampMax && InValue > ClampMaxValue)
		{
			return ClampMaxValue;
		}
		return InValue;
	}
};
