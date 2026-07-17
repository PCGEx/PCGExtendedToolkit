// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathMean.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExDetailsNoise.generated.h"

#pragma region PCG exposition
// Exposed copy of the otherwise private PCG' spatial noise mode enum
UENUM(BlueprintType)
enum class PCGExSpatialNoiseMode : uint8
{
	/** Your classic perlin noise. */
	Perlin,
	/** Based on underwater fake caustic rendering, gives swirly look. */
	Caustic,
	/** Voronoi noise, result a the distance to edge and cell ID. */
	Voronoi,
	/** Based on fractional brownian motion. */
	FractionalBrownian,
	/** Used to create masks to blend out edges. */
	EdgeMask,
};

UENUM(BlueprintType)
enum class PCGExSpatialNoiseMask2DMode : uint8
{
	/** Your classic perlin noise. */
	Perlin,
	/** Based on underwater fake caustic rendering, gives swirly look. */
	Caustic,
	/** Based on fractional brownian motion. */
	FractionalBrownian,
};

#pragma endregion

struct FPCGExContext;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExRandomRatioDetails
{
	GENERATED_BODY()

	FPCGExRandomRatioDetails() = default;

	explicit FPCGExRandomRatioDetails(double DefaultAmount)
	{
		Amount_DEPRECATED = DefaultAmount;
	}

	/** Seed for random number generation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorInteger32 BaseSeed = FPCGExInputShorthandSelectorInteger32(FName("@Data.Seed"), 42);

	/** How amount is interpreted: Relative = 0-1 percentage, Discrete = fixed count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExMeanMeasure Units = EPCGExMeanMeasure::Relative;

	/** Percentage of items to pick (0 = none, 1 = all). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Amount (Relative)", EditCondition="Units == EPCGExMeanMeasure::Relative", EditConditionHides))
	FPCGExInputShorthandSelectorDouble01 RelativeAmount = FPCGExInputShorthandSelectorDouble01(FName("@Data.Amount"), 0.5, false);

	/** Exact number of items to pick. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Amount (Fixed)", EditCondition="Units == EPCGExMeanMeasure::Discrete", EditConditionHides))
	FPCGExInputShorthandSelectorInteger32Abs DiscreteAmount = FPCGExInputShorthandSelectorInteger32Abs(FName("@Data.Amount"), 42, false);

	/** Enable minimum count constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoClampMin = false;

	/** Minimum number of items to pick regardless of relative amount. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoClampMin"))
	FPCGExInputShorthandSelectorInteger32Abs ClampMin = FPCGExInputShorthandSelectorInteger32Abs(FName("@Data.ClampMin"), 1, false);

	/** Enable maximum count constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoClampMax = false;

	/** Maximum number of items to pick regardless of relative amount. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoClampMax"))
	FPCGExInputShorthandSelectorInteger32Abs ClampMax = FPCGExInputShorthandSelectorInteger32Abs(FName("@Data.ClampMax"), 500, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType SeedInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalSeed_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 SeedValue_DEPRECATED = 42;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType AmountInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalAmount_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Amount_DEPRECATED = 0.5;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 FixedAmount_DEPRECATED = 1;

#pragma endregion

	int32 GetNumPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems) const;

	void GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TSet<int32>& OutPicks) const;
	void GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TArray<int32>& OutPicks) const;

#if WITH_EDITOR
	void ApplyDeprecation();
	/** Rewires the pre-shorthand override pins; call from the embedder's PCGExApplyDeprecationBeforeUpdatePins under the same version gate as ApplyDeprecation. */
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};
