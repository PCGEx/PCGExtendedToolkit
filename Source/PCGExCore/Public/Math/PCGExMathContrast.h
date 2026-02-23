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

namespace PCGExMath
{
	namespace Contrast
	{
		//
		// Core contrast functions -- input in [-1, 1], output in [-1, 1]
		// Contrast parameter: 1.0 = no change, >1 = more contrast, <1 = less contrast
		//

		/**
		 * Power-based contrast (simple, predictable)
		 * Formula: sign(v) * |v|^(1/c)
		 * @param Value Input value in [-1, 1]
		 * @param Contrast Contrast amount (1.0 = no change, >1 = more contrast)
		 */
		FORCEINLINE double ContrastPower(const double Value, const double Contrast)
		{
			if (Contrast <= SMALL_NUMBER || FMath::Abs(Value) < SMALL_NUMBER) { return Value; }
			const double Exp = 1.0 / Contrast;
			return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), Exp);
		}

		/**
		 * S-curve contrast using tanh (smooth, never clips)
		 * Formula: tanh(v * c) / tanh(c)
		 * @param Value Input value in [-1, 1]
		 * @param Contrast Contrast amount (1.0 = no change, >1 = more contrast)
		 */
		FORCEINLINE double ContrastSCurve(const double Value, const double Contrast)
		{
			if (Contrast <= SMALL_NUMBER) { return Value; }
			const double TanhC = FMath::Tanh(Contrast);
			if (FMath::Abs(TanhC) < SMALL_NUMBER) { return Value; }
			return FMath::Tanh(Value * Contrast) / TanhC;
		}

		/**
		 * Attempt contrast using sine-based gain function (symmetrical S-curve)
		 * Good for subtle adjustments, softer than sigmoid
		 * @param Value Input value in [-1, 1]
		 * @param Contrast Contrast amount (1.0 = no change, >1 = more contrast)
		 */
		FORCEINLINE double ContrastGain(const double Value, const double Contrast)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }

			// Remap to [0,1] for gain calculation
			const double T = Value * 0.5 + 0.5;

			// Attempt function for S-curve: attempt = 0.5 * ((2*t)^(1/c)) for t < 0.5
			double Result;
			if (T < 0.5)
			{
				Result = 0.5 * FMath::Pow(2.0 * T, Contrast);
			}
			else
			{
				Result = 1.0 - 0.5 * FMath::Pow(2.0 * (1.0 - T), Contrast);
			}

			// Remap back to [-1,1]
			return Result * 2.0 - 1.0;
		}

		/**
		 * Apply contrast with selectable curve type -- input in [-1, 1]
		 * @param Value Input value in [-1, 1]
		 * @param Contrast Contrast amount (1.0 = no change)
		 * @param CurveType 0 = Power, 1 = SCurve, 2 = Gain
		 */
		FORCEINLINE double ApplyContrast(const double Value, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }

			switch (CurveType)
			{
			case 0: return ContrastPower(Value, Contrast);
			case 1: return ContrastSCurve(Value, Contrast);
			case 2: return ContrastGain(Value, Contrast);
			default: return ContrastPower(Value, Contrast);
			}
		}

		//
		// Vector overloads -- [-1,1] per component
		//

		FORCEINLINE FVector2D ApplyContrast(const FVector2D& Value, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }
			return FVector2D(
				ApplyContrast(Value.X, Contrast, CurveType),
				ApplyContrast(Value.Y, Contrast, CurveType)
			);
		}

		FORCEINLINE FVector ApplyContrast(const FVector& Value, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }
			return FVector(
				ApplyContrast(Value.X, Contrast, CurveType),
				ApplyContrast(Value.Y, Contrast, CurveType),
				ApplyContrast(Value.Z, Contrast, CurveType)
			);
		}

		FORCEINLINE FVector4 ApplyContrast(const FVector4& Value, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }
			return FVector4(
				ApplyContrast(Value.X, Contrast, CurveType),
				ApplyContrast(Value.Y, Contrast, CurveType),
				ApplyContrast(Value.Z, Contrast, CurveType),
				ApplyContrast(Value.W, Contrast, CurveType)
			);
		}

		//
		// Arbitrary range -- remaps [Min,Max] → [-1,1] internally
		//

		FORCEINLINE double ApplyContrastInRange(const double Value, const double Contrast, const int32 CurveType, const double Min, const double Max)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return Value; }
			const double Range = Max - Min;
			if (Range <= SMALL_NUMBER) { return Value; }
			const double Normalized = (Value - Min) / Range * 2.0 - 1.0;
			return (ApplyContrast(Normalized, Contrast, CurveType) + 1.0) * 0.5 * Range + Min;
		}

		//
		// [-1,1] batch operations (switch outside loop for branch prediction)
		//

		inline void ApplyContrastBatch(double* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return; }

			switch (CurveType)
			{
			case 0: // Power
				{
					const double Exp = 1.0 / Contrast;
					for (int32 i = 0; i < Count; ++i)
					{
						const double V = Values[i];
						if (FMath::Abs(V) > SMALL_NUMBER)
						{
							Values[i] = FMath::Sign(V) * FMath::Pow(FMath::Abs(V), Exp);
						}
					}
				}
				break;

			case 1: // SCurve
				{
					const double TanhC = FMath::Tanh(Contrast);
					const double InvTanhC = 1.0 / TanhC;
					for (int32 i = 0; i < Count; ++i)
					{
						Values[i] = FMath::Tanh(Values[i] * Contrast) * InvTanhC;
					}
				}
				break;

			case 2: // Gain
				for (int32 i = 0; i < Count; ++i)
				{
					Values[i] = ContrastGain(Values[i], Contrast);
				}
				break;

			default:
				ApplyContrastBatch(Values, Count, Contrast, 0);
				break;
			}
		}

		inline void ApplyContrastBatch(FVector2D* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return; }

			for (int32 i = 0; i < Count; ++i)
			{
				Values[i] = ApplyContrast(Values[i], Contrast, CurveType);
			}
		}

		inline void ApplyContrastBatch(FVector* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return; }

			for (int32 i = 0; i < Count; ++i)
			{
				Values[i] = ApplyContrast(Values[i], Contrast, CurveType);
			}
		}

		inline void ApplyContrastBatch(FVector4* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return; }

			for (int32 i = 0; i < Count; ++i)
			{
				Values[i] = ApplyContrast(Values[i], Contrast, CurveType);
			}
		}

		//
		// [Min,Max] batch -- fused remap, switch outside loop
		// Pass 1: find min/max if not provided. Pass 2: remap + contrast + unmap.
		//

		inline void ApplyContrastBatchInRange(double* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType, const double Min, const double Max)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER)) { return; }

			const double Range = Max - Min;
			if (Range <= SMALL_NUMBER) { return; }

			const double InvRange = 1.0 / Range;

			switch (CurveType)
			{
			case 0: // Power
				{
					const double Exp = 1.0 / Contrast;
					for (int32 i = 0; i < Count; ++i)
					{
						const double V = (Values[i] - Min) * InvRange * 2.0 - 1.0;
						const double C = FMath::Abs(V) > SMALL_NUMBER
							                 ? FMath::Sign(V) * FMath::Pow(FMath::Abs(V), Exp)
							                 : V;
						Values[i] = (C + 1.0) * 0.5 * Range + Min;
					}
				}
				break;

			case 1: // SCurve
				{
					const double TanhC = FMath::Tanh(Contrast);
					const double InvTanhC = 1.0 / TanhC;
					for (int32 i = 0; i < Count; ++i)
					{
						const double V = (Values[i] - Min) * InvRange * 2.0 - 1.0;
						Values[i] = (FMath::Tanh(V * Contrast) * InvTanhC + 1.0) * 0.5 * Range + Min;
					}
				}
				break;

			case 2: // Gain
				for (int32 i = 0; i < Count; ++i)
				{
					const double V = (Values[i] - Min) * InvRange * 2.0 - 1.0;
					Values[i] = (ContrastGain(V, Contrast) + 1.0) * 0.5 * Range + Min;
				}
				break;

			default:
				ApplyContrastBatchInRange(Values, Count, Contrast, 0, Min, Max);
				break;
			}
		}

		/**
		 * Auto-range batch: scans for min/max, then applies contrast preserving the original range.
		 */
		inline void ApplyContrastBatchAutoRange(double* RESTRICT Values, const int32 Count, const double Contrast, const int32 CurveType = 0)
		{
			if (FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER) || Count <= 0) { return; }

			double Min = Values[0];
			double Max = Values[0];
			for (int32 i = 1; i < Count; ++i)
			{
				Min = FMath::Min(Min, Values[i]);
				Max = FMath::Max(Max, Values[i]);
			}

			ApplyContrastBatchInRange(Values, Count, Contrast, CurveType, Min, Max);
		}

		//
		// TArrayView convenience overloads
		//

		inline void ApplyContrastBatch(TArrayView<double> Values, const double Contrast, const int32 CurveType = 0)
		{
			ApplyContrastBatch(Values.GetData(), Values.Num(), Contrast, CurveType);
		}

		inline void ApplyContrastBatch(TArrayView<FVector2D> Values, const double Contrast, const int32 CurveType = 0)
		{
			ApplyContrastBatch(Values.GetData(), Values.Num(), Contrast, CurveType);
		}

		inline void ApplyContrastBatch(TArrayView<FVector> Values, const double Contrast, const int32 CurveType = 0)
		{
			ApplyContrastBatch(Values.GetData(), Values.Num(), Contrast, CurveType);
		}

		inline void ApplyContrastBatch(TArrayView<FVector4> Values, const double Contrast, const int32 CurveType = 0)
		{
			ApplyContrastBatch(Values.GetData(), Values.Num(), Contrast, CurveType);
		}

		inline void ApplyContrastBatchInRange(TArrayView<double> Values, const double Contrast, const int32 CurveType, const double Min, const double Max)
		{
			ApplyContrastBatchInRange(Values.GetData(), Values.Num(), Contrast, CurveType, Min, Max);
		}

		inline void ApplyContrastBatchAutoRange(TArrayView<double> Values, const double Contrast, const int32 CurveType = 0)
		{
			ApplyContrastBatchAutoRange(Values.GetData(), Values.Num(), Contrast, CurveType);
		}
	}
}
