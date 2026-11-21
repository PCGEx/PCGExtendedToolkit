// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExDistancesDetails.generated.h"

namespace PCGExMath
{
	class IDistances;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExDistanceDetails
{
	GENERATED_BODY()

	FPCGExDistanceDetails()
	{
	}

	explicit FPCGExDistanceDetails(const EPCGExDistance SourceMethod, const EPCGExDistance TargetMethod)
		: Source(SourceMethod)
		  , Target(TargetMethod)
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistance Source = EPCGExDistance::Center;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistance Target = EPCGExDistance::Center;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistanceType Type = EPCGExDistanceType::Euclidian;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOverlapIsZero = true;

	const PCGExMath::IDistances* MakeDistances() const;
};
