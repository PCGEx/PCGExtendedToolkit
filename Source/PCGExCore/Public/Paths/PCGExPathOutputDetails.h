// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExPathOutputDetails.generated.h"

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="paths/common-settings/paths-output-settings"))
struct PCGEXCORE_API FPCGExPathOutputDetails
{
	GENERATED_BODY()

	FPCGExPathOutputDetails() = default;

	/** Don't output paths if they have less points than a specified amount. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bRemoveSmallPaths = false;

	/** Minimum points threshold */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, EditCondition="bRemoveSmallPaths", ClampMin=2))
	int32 MinPointCount = 3;

	/** Don't output paths if they have more points than a specified amount. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bRemoveLargePaths = false;

	/** Maximum points threshold */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, EditCondition="bRemoveLargePaths", ClampMin=2))
	int32 MaxPointCount = 500;

	bool Validate(int32 NumPathPoints) const;
};
