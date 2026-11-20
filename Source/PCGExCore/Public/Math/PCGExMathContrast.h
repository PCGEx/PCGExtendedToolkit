// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGExMathContrast.generated.h"

/**
 * Contrast curve types for noise adjustment
 */
UENUM(BlueprintType)
enum class EPCGExContrastCurve : uint8
{
	Power UMETA(DisplayName = "Power", ToolTip = "Power curve - simple and predictable"),
	SCurve UMETA(DisplayName = "S-Curve (Sigmoid)", ToolTip = "Smooth S-curve using tanh - never clips"),
	Gain UMETA(DisplayName = "Gain", ToolTip = "Attempt function S-curve - symmetrical, subtle"),
};

namespace PCGExMath::Contrast
{
	//
	// Core contrast functions -- input in [0, 1], output in [0, 1]
	// Contrast parameter: 1.0 = no change, >1 = more contrast, <1 = less contrast
	//

	/**
		 * Gain contrast using power-based S-curve (symmetrical)
		 * Good for subtle adjustments, softer than sigmoid
		 * @param Value Input value in [0, 1]
		 * @param Contrast Contrast amount (1.0 = no change, >1 = more contrast)
		 */
	FORCEINLINE double ContrastGain(const double Value, const double Contrast)
	{
		if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER))
		{
			return Value;
		}

		if (Value < 0.5)
		{
			return 0.5 * FMath::Pow(2.0 * Value, Contrast);
		}
		return 1.0 - 0.5 * FMath::Pow(2.0 * (1.0 - Value), Contrast);
	}

	/**
	 * Curve constants hoisted out of the per-value call. CurveType: 1 = SCurve, 2 = Gain, anything else = Power.
	 * The S-curve multiplies by a cached reciprocal instead of dividing, as ApplyContrastBatchInRange does.
	 */
	struct FPreparedContrast
	{
		void Init(const double InContrast, const int32 InCurveType)
		{
			Contrast = InContrast;
			CurveType = InCurveType;
			bIdentity = FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER);
			InvContrast = Contrast > SMALL_NUMBER ? 1.0 / Contrast : 1.0;
			const double TanhC = FMath::Tanh(Contrast);
			InvTanhC = FMath::Abs(TanhC) < SMALL_NUMBER ? 0.0 : 1.0 / TanhC;
		}

		FORCEINLINE double Apply(const double Value) const
		{
			if (bIdentity)
			{
				return Value;
			}

			switch (CurveType)
			{
			case 1:
				{
					if (Contrast <= SMALL_NUMBER || InvTanhC == 0.0)
					{
						return Value;
					}
					const double T = Value * 2.0 - 1.0;
					return (FMath::Tanh(T * Contrast) * InvTanhC) * 0.5 + 0.5;
				}
			case 2:
				return ContrastGain(Value, Contrast);
			default:
				{
					if (Contrast <= SMALL_NUMBER)
					{
						return Value;
					}
					const double T = Value * 2.0 - 1.0;
					if (FMath::Abs(T) < SMALL_NUMBER)
					{
						return Value;
					}
					return (FMath::Sign(T) * FMath::Pow(FMath::Abs(T), InvContrast)) * 0.5 + 0.5;
				}
			}
		}

	private:
		double Contrast = 1.0;
		int32 CurveType = 0;
		double InvContrast = 1.0;
		double InvTanhC = 1.0;
		bool bIdentity = true;
	};

	//
	// [Min,Max] batch -- fused remap + contrast + unmap, switch outside loop
	//

	inline void ApplyContrastBatchInRange(double* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType, const double Min, const double Max)
	{
		if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER))
		{
			return;
		}

		const double Range = Max - Min;
		if (Range <= SMALL_NUMBER)
		{
			return;
		}

		const double InvRange = 1.0 / Range;

		switch (CurveType)
		{
		case 0: // Power
		{
			// Same degenerate guard as FPreparedContrast::Apply.
			if (Contrast <= SMALL_NUMBER)
			{
				break;
			}
			const double Exp = 1.0 / Contrast;
			for (int32 i = 0; i < Count; ++i)
			{
				const double Norm = (Values[i] - Min) * InvRange;
				const double T = Norm * 2.0 - 1.0;
				const double C = FMath::Abs(T) > SMALL_NUMBER
					? FMath::Sign(T) * FMath::Pow(FMath::Abs(T), Exp)
					: T;
				Values[i] = (C * 0.5 + 0.5) * Range + Min;
			}
		}
		break;

		case 1: // SCurve
		{
			// Same degenerate guards as FPreparedContrast::Apply.
			const double TanhC = FMath::Tanh(Contrast);
			if (Contrast <= SMALL_NUMBER || FMath::Abs(TanhC) < SMALL_NUMBER)
			{
				break;
			}
			const double InvTanhC = 1.0 / TanhC;
			for (int32 i = 0; i < Count; ++i)
			{
				const double Norm = (Values[i] - Min) * InvRange;
				const double T = Norm * 2.0 - 1.0;
				Values[i] = (FMath::Tanh(T * Contrast) * InvTanhC * 0.5 + 0.5) * Range + Min;
			}
		}
		break;

		case 2: // Gain
			for (int32 i = 0; i < Count; ++i)
			{
				const double Norm = (Values[i] - Min) * InvRange;
				Values[i] = ContrastGain(Norm, Contrast) * Range + Min;
			}
			break;

		default:
			ApplyContrastBatchInRange(Values, Count, Contrast, 0, Min, Max);
			break;
		}
	}

	//
	// TArrayView convenience overload
	//

	inline void ApplyContrastBatchInRange(TArrayView<double> Values, const double Contrast, const int32 CurveType, const double Min, const double Max)
	{
		ApplyContrastBatchInRange(Values.GetData(), Values.Num(), Contrast, CurveType, Min, Max);
	}
}
