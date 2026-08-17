// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExWeightCurve.generated.h"

/**
 * Weight multiplier over a normalized [0,1] domain -- the shared shape for "prefer somewhere along a
 * known min/max axis" knobs. Hosts declare a member of this type and get the inline PCGEx curve
 * editor for free (PCGExPropertiesEditor registers the layout by struct name).
 *
 * Optional clamp metas on the HOST UPROPERTY constrain editing (absent = that side is free):
 *   meta = (PCGExCurveTimeMin="0", PCGExCurveTimeMax="1", PCGExCurveValueMin="0", PCGExCurveValueMax="10")
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExWeightCurve
{
	GENERATED_BODY()

	/** Neutral: a single flat 1.0 multiplier. */
	FPCGExWeightCurve();

	/** Linear ramp from InValueAtZero (t=0) to InValueAtOne (t=1). */
	FPCGExWeightCurve(float InValueAtZero, float InValueAtOne);

	/** Multiplier over the normalized [0,1] domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FRuntimeFloatCurve Curve;

	/** Curve value at InTime. A key-less curve evaluates neutral (1.0) -- never the FRichCurve default 0.
	 *  Cold paths only; loops should go through MakeLookup once and eval the lookup. */
	float Eval(float InTime) const;

	/** Lookup for loop evaluation. A key-less curve bakes as flat neutral (1.0). Default 2-key linear
	 *  ramps resolve to a closed form regardless of mode; pick Lookup only when eval count amortizes
	 *  the bake. Never returns null. */
	PCGExFloatLUT MakeLookup(EPCGExCurveLUTMode InMode = EPCGExCurveLUTMode::Direct, int32 InNumSamples = 64) const;

	/** Lookup with a per-sample transform folded into the bake (always a real LUT, exact endpoints).
	 *  Same key-less-is-neutral guarantee: the transform then applies to a flat 1.0. Never returns null. */
	PCGExFloatLUT MakeLookup(TFunctionRef<float(float)> InSampleTransform, int32 InNumSamples = 64) const;
};
