// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Utils/PCGExCompare.h"
#include "PCGExCompareShorthandsDetails.generated.h"

namespace PCGExData
{
	class FPointIO;
}

struct FPCGExContext;

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExCompareSelectorDouble : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	FPCGExCompareSelectorDouble() = default;

	explicit FPCGExCompareSelectorDouble(const FString& DefaultName)
	{
		Attribute.Update(DefaultName);
	}

	FPCGExCompareSelectorDouble(const FString& DefaultName, const double DefaultValue)
		: FPCGExCompareSelectorDouble(DefaultName)
	{
		Constant = DefaultValue;
	}

	PCGEX_SETTING_VALUE_DECL(, double)
	bool TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, double& OutValue, const bool bQuiet = false) const;

	/** The comparison operator to use. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Constant comparison value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Constant = 0;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	FORCEINLINE bool Compare(const double A, const double B) const
	{
		return PCGExCompare::Compare(Comparison, A, B, Tolerance);
	}

#if WITH_EDITOR
	FString GetDisplayNamePostfix() const;
#endif

	void RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;
};
