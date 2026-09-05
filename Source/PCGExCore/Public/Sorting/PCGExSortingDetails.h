// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExSortingCommon.h"
#include "Details/PCGExAttributesDetails.h"

#include "PCGExSortingDetails.generated.h"

struct FPCGContext;

namespace PCGExData
{
	class FPointIOCollection;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExSortRuleConfig : public FPCGExInputConfig
{
	GENERATED_BODY()

	FPCGExSortRuleConfig() = default;

	/** If enabled, reads the sort value from a data tag (tag:value format) instead of a point attribute.
	 * Tag values are per-data, so all points in the same data will share the same sort value.
	 * The Attribute selector will be used as the tag name to look up. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bReadDataTag = false;

	/** Equality tolerance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Invert sorting direction on that rule. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvertRule = false;

};

namespace PCGExSorting
{
	PCGEXCORE_API void DeclareSortingRulesInputs(TArray<FPCGPinProperties>& PinProperties, const EPCGPinStatus InStatus);

	PCGEXCORE_API TArray<FPCGExSortRuleConfig> GetSortingRules(FPCGExContext* InContext, const FName InLabel);
}

#undef PCGEX_UNSUPPORTED_STRING_TYPES
#undef PCGEX_UNSUPPORTED_PATH_TYPES
