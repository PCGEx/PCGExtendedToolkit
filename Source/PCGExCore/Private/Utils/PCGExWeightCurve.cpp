// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Utils/PCGExWeightCurve.h"

namespace PCGExWeightCurve
{
	FRuntimeFloatCurve MakeNeutralCurve()
	{
		FRuntimeFloatCurve Neutral;
		Neutral.EditorCurveData.AddKey(0.0f, 1.0f);
		return Neutral;
	}
}

FPCGExWeightCurve::FPCGExWeightCurve()
{
	Curve.EditorCurveData.AddKey(0.0f, 1.0f);
}

FPCGExWeightCurve::FPCGExWeightCurve(const float InValueAtZero, const float InValueAtOne)
{
	Curve.EditorCurveData.AddKey(0.0f, InValueAtZero);
	Curve.EditorCurveData.AddKey(1.0f, InValueAtOne);
}

float FPCGExWeightCurve::Eval(const float InTime) const
{
	const FRichCurve* Rich = Curve.GetRichCurveConst();
	return (Rich && Rich->GetNumKeys() > 0) ? Rich->Eval(InTime) : 1.0f;
}

PCGExFloatLUT FPCGExWeightCurve::MakeLookup(const EPCGExCurveLUTMode InMode, const int32 InNumSamples) const
{
	// Key-less curves fall back to a flat neutral stand-in: the raw lookup's key-less path
	// evaluates 0, which for a weight multiplier would veto everything.
	const FRichCurve* Rich = Curve.GetRichCurveConst();
	if (!Rich || Rich->GetNumKeys() == 0)
	{
		return FPCGExCurveFloatLookup::Make(PCGExWeightCurve::MakeNeutralCurve(), EPCGExCurveLUTMode::Direct, InNumSamples);
	}
	return FPCGExCurveFloatLookup::Make(Curve, InMode, InNumSamples);
}

PCGExFloatLUT FPCGExWeightCurve::MakeLookup(const TFunctionRef<float(float)> InSampleTransform, const int32 InNumSamples) const
{
	const FRichCurve* Rich = Curve.GetRichCurveConst();
	if (!Rich || Rich->GetNumKeys() == 0)
	{
		return FPCGExCurveFloatLookup::Make(PCGExWeightCurve::MakeNeutralCurve(), InSampleTransform, InNumSamples);
	}
	return FPCGExCurveFloatLookup::Make(Curve, InSampleTransform, InNumSamples);
}
