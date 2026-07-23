// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGExMath.generated.h"

namespace PCGExData
{
	struct FProxyPoint;
	class FFacade;
	struct FConstPoint;

	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExIndexSafety : uint8
{
	Ignore = 0 UMETA(DisplayName = "Ignore", Tooltip="Out of bounds indices are ignored.(0,1,2,-1,-1,-1,...)"),
	Tile   = 1 UMETA(DisplayName = "Tile", Tooltip="Out of bounds indices are tiled. (0,1,2,0,1,2...)"),
	Clamp  = 2 UMETA(DisplayName = "Clamp", Tooltip="Out of bounds indices are clamped. (0,1,2,2,2,2...)"),
	Yoyo   = 3 UMETA(DisplayName = "Yoyo", Tooltip="Out of bounds indices are mirrored and back. (0,1,2,1,0,1...)"),
};

UENUM()
enum class EPCGExTruncateMode : uint8
{
	None  = 0 UMETA(DisplayName = "None", ToolTip="None", ActionIcon="Unchanged"),
	Round = 1 UMETA(DisplayName = "Round", ToolTip="Round", ActionIcon="Round"),
	Ceil  = 2 UMETA(DisplayName = "Ceil", ToolTip="Ceil", ActionIcon="Ceil"),
	Floor = 3 UMETA(DisplayName = "Floor", ToolTip="Floor", ActionIcon="Floor"),
};

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGExIntersectionStrictness : uint8
{
	Loose  = 0 UMETA(DisplayName = "Loose", ToolTip="Consider intersections only through segment/segment distance."),
	MainA  = 1 << 0 UMETA(DisplayName = "Strict on Main A", ToolTip="Intersections located on main segment' start point are considered invalid."),
	MainB  = 1 << 1 UMETA(DisplayName = "Strict on Main B", ToolTip="Intersections located on main segment' end point are considered invalid."),
	OtherA = 1 << 2 UMETA(DisplayName = "Strict on Other A", ToolTip="Intersections located on end segment' start point are considered invalid."),
	OtherB = 1 << 3 UMETA(DisplayName = "Strict on Other B", ToolTip="Intersections located on end segment' end point are considered invalid."),
	Strict = MainA | MainB | OtherA | OtherB
};

ENUM_CLASS_FLAGS(EPCGExIntersectionStrictness)
using EPCGExIntersectionStrictnessBitmask = TEnumAsByte<EPCGExIntersectionStrictness>;

namespace PCGExMath
{
	struct PCGEXCORE_API FClosestPosition
	{
		bool bValid = false;
		int32 Index = -1;
		FVector Origin = FVector::ZeroVector;
		FVector Location = FVector::ZeroVector;
		double DistSquared = TNumericLimits<double>::Max();

		FClosestPosition() = default;
		explicit FClosestPosition(const FVector& InOrigin);
		FClosestPosition(const FVector& InOrigin, const FVector& InLocation);
		FClosestPosition(const FVector& InOrigin, const FVector& InLocation, const int32 InIndex);

		bool Update(const FVector& InLocation);
		bool Update(const FVector& InLocation, const int32 InIndex);

		FVector Direction() const
		{
			return (Origin - Location).GetSafeNormal();
		}

		friend bool operator<(const FClosestPosition& A, const FClosestPosition& B)
		{
			return A.DistSquared < B.DistSquared;
		}

		friend bool operator>(const FClosestPosition& A, const FClosestPosition& B)
		{
			return A.DistSquared > B.DistSquared;
		}

		operator FVector() const
		{
			return Location;
		}

		operator double() const
		{
			return DistSquared;
		}

		operator bool() const
		{
			return bValid;
		}
	};

	struct PCGEXCORE_API FSegment
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		FVector Direction = FVector::ZeroVector;
		FBox Bounds = FBox(ForceInit);

		FSegment() = default;
		FSegment(const FVector& InA, const FVector& InB, const double Expansion = 0);

		double Dot(const FVector& InDirection) const
		{
			return FVector::DotProduct(Direction, InDirection);
		}

		double Dot(const FSegment& InSegment) const
		{
			return FVector::DotProduct(Direction, InSegment.Direction);
		}

		FVector Lerp(const double InLerp) const
		{
			return FMath::Lerp(A, B, InLerp);
		}

		bool FindIntersection(const FVector& A2, const FVector& B2, const double SquaredTolerance, FVector& OutSelf, FVector& OutOther, const uint8 Strictness) const;
		bool FindIntersection(const FSegment& S, const double SquaredTolerance, FVector& OutSelf, FVector& OutOther, const uint8 Strictness) const;
	};

	PCGEXCORE_API double TruncateDbl(const double Value, const EPCGExTruncateMode Mode);

#pragma region basics

	FORCEINLINE static double FastRand01(uint32& Seed)
	{
		Seed = Seed * 1664525u + 1013904223u;
		return (Seed & 0x00FFFFFF) / static_cast<double>(0x01000000);
	}

	FORCEINLINE static FVector RandomPointInSphere(const FVector& Center, const double Radius, uint32& Seed)
	{
		for (int i = 0; i < 10; ++i)
		{
			const float x = (FastRand01(Seed) * 2.0f - 1.0f);
			const float y = (FastRand01(Seed) * 2.0f - 1.0f);
			const float z = (FastRand01(Seed) * 2.0f - 1.0f);

			FVector V(x, y, z);

			if (V.SizeSquared() <= 1.0f)
			{
				return Center + V * Radius;
			}
		}

		return Center;
	}

	FORCEINLINE static double DegreesToDot(const double AngleInDegrees)
	{
		return FMath::Cos(FMath::DegreesToRadians(AngleInDegrees));
	}

	FORCEINLINE static double DotToDegrees(const double Dot)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0, 1.0)));
	}

	PCGEXCORE_API double ConvertStringToDouble(const FString& StringToConvert);

	// Remap function
	FORCEINLINE static double Remap(const double InBase, const double InMin, const double InMax, const double OutMin = 0, const double OutMax = 1)
	{
		return FMath::Lerp(OutMin, OutMax, (InBase - InMin) / (InMax - InMin));
	}

	template <typename T>
	FORCEINLINE static T Tile(const T Value, const T Min, const T Max)
	{
		if constexpr (std::is_unsigned_v<T>)
		{
			return ((Value - Min) % (Max - Min + 1)) + Min;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			T R = Max - Min + 1;
			T W = (Value - Min) % R;
			return W < 0 ? W + R + Min : W + Min;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			T R = Max - Min;
			T W = FMath::Fmod(Value - Min, R);
			return W < 0 ? W + R + Min : W + Min;
		}
		else if constexpr (std::is_same_v<T, FVector2D>)
		{
			return FVector2D(Tile(Value.X, Min.X, Max.X), Tile(Value.Y, Min.Y, Max.Y));
		}
		else if constexpr (std::is_same_v<T, FVector>)
		{
			return FVector(Tile(Value.X, Min.X, Max.X), Tile(Value.Y, Min.Y, Max.Y), Tile(Value.Z, Min.Z, Max.Z));
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return FVector4(Tile(Value.X, Min.X, Max.X), Tile(Value.Y, Min.Y, Max.Y), Tile(Value.Z, Min.Z, Max.Z), Tile(Value.W, Min.W, Max.W));
		}
		else
		{
			static_assert(std::is_unsigned_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector> || std::is_same_v<T, FVector4>, "Can't tile type.");
			return T{};
		}
	}

	template <typename T>
	FORCEINLINE static int SignPlus(const T& InValue)
	{
		const int Sign = FMath::Sign(InValue);
		return Sign == 0 ? 1 : Sign;
	}

	template <typename T>
	FORCEINLINE static int SignMinus(const T& InValue)
	{
		const int Sign = FMath::Sign(InValue);
		return Sign == 0 ? -1 : Sign;
	}

	FORCEINLINE static FBox ConeBox(const FVector& Center, const FVector& ConeDirection, const double Size)
	{
		const FVector Dir = ConeDirection.GetSafeNormal();
		const FVector U = FVector::CrossProduct(Dir, Dir + FVector(0.1)).GetSafeNormal();
		const FVector V = FVector::CrossProduct(Dir, Dir + FVector(-0.1)).GetSafeNormal();

		FBox Box = FBox(Center - FVector(0.0001), Center + FVector(0.0001));
		Box += Center + Dir * Size;
		Box += Center + U * Size;
		Box += Center + V * Size;

		//UE_LOG(LogPCGEx, Warning, TEXT("Box Min X:%f, Y:%f, Z:%f | Max X:%f, Y:%f, Z:%f"), Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z);

		return Box;
	}

	template <typename T>
	FORCEINLINE static void GetMinMax(const TArray<T>& Values, T& OutMin, T& OutMax)
	{
		OutMin = TNumericLimits<T>::Max();
		OutMax = TNumericLimits<T>::Min();
		for (const T Value : Values)
		{
			OutMin = FMath::Min(OutMin, Value);
			OutMax = FMath::Max(OutMax, Value);
		}
	}

	template <typename T>
	FORCEINLINE static void SignedNormalize(TArray<T> Values)
	{
		T Min;
		T Max;
		GetMinMax(Values, Min, Max);
		T Range = FMath::Max(FMath::Abs(Max), FMath::Abs(Min));
		for (int i = 0; i < Values.Num(); i++)
		{
			Values[i] = Values[i] / Range;
		}
	}

	template <typename T>
	FORCEINLINE static void Remap(TArray<T> Values, const bool bZeroMin = false, T Range = 1)
	{
		T Min;
		T Max;
		GetMinMax(Values, Min, Max);
		if (bZeroMin)
		{
			for (int i = 0; i < Values.Num(); i++)
			{
				Values[i] = Remap(Values[i], 0, Max, 0, 1) * Range;
			}
		}
		else
		{
			for (int i = 0; i < Values.Num(); i++)
			{
				Values[i] = Remap(Values[i], Min, Max, 0, 1) * Range;
			}
		}
	}

	template <typename T>
	FORCEINLINE static void Remap(TArray<T> Values, T Min, T Max, T Range = 1)
	{
		for (int i = 0; i < Values.Num(); i++)
		{
			Values[i] = Remap(Values[i], Min, Max, 0, 1) * Range;
		}
	}

	PCGEXCORE_API FVector SafeLinePlaneIntersection(const FVector& Pt1, const FVector& Pt2, const FVector& PlaneOrigin, const FVector& PlaneNormal, bool& bIntersect);


	PCGEXCORE_API bool SphereOverlap(const FSphere& S1, const FSphere& S2, double& OutOverlap);
	PCGEXCORE_API bool SphereOverlap(const FBoxSphereBounds& S1, const FBoxSphereBounds& S2, double& OutOverlap);


#pragma endregion

#pragma region Rounding

	FORCEINLINE static void Snap(double& Value, const double Step)
	{
		Value = !FMath::IsNearlyZero(Step) ? FMath::RoundToDouble(Value / Step) * Step : Value;
	}

	FORCEINLINE static double Round10(const float A)
	{
		return FMath::RoundToFloat(A * 10.0f) / 10.0f;
	}

	FORCEINLINE static FVector Round10(const FVector& A)
	{
		return FVector(Round10(A.X), Round10(A.Y), Round10(A.Z));
	}


#pragma endregion

#pragma region Distances

	FORCEINLINE double DistanceManhattan(const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}

	FORCEINLINE double DistanceManhattan(const FVector& A, const FVector& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y) + FMath::Abs(A.Z - B.Z);
	}

	FORCEINLINE double DistanceChebyshev(const FVector2D& A, const FVector2D& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}

	FORCEINLINE double DistanceChebyshev(const FVector& A, const FVector& B)
	{
		return FMath::Max3(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z));
	}

#pragma endregion

	template <typename T>
	FORCEINLINE void ReverseRange(TArray<T>& InArray, int32 Start, int32 End)
	{
		while (Start < End)
		{
			InArray.Swap(Start, End);
			Start++;
			End--;
		}
	}

	template <typename T, EPCGExIndexSafety Safety = EPCGExIndexSafety::Ignore>
	FORCEINLINE static T SanitizeIndex(const T& Index, const T& MaxIndex)
	{
		if constexpr (Safety == EPCGExIndexSafety::Yoyo)
		{
			const T L = FMath::Max<T>(2 * MaxIndex, 1);
			T C = Index % L;
			if constexpr (std::is_signed_v<T>)
			{
				C += C < 0 ? L : 0;
			}
			return C <= MaxIndex ? C : L - C;
		}
		else if constexpr (Safety == EPCGExIndexSafety::Tile)
		{
			return PCGExMath::Tile(Index, 0, MaxIndex);
		}
		else if constexpr (Safety == EPCGExIndexSafety::Clamp)
		{
			return FMath::Clamp(Index, 0, MaxIndex);
		}
		else
		{
			return Index < 0 || Index > MaxIndex ? -1 : Index;
		}
	}

	template <typename T>
	FORCEINLINE static T SanitizeIndex(const T& Index, const T& MaxIndex, const EPCGExIndexSafety Method)
	{
		switch (Method)
		{
		default: case EPCGExIndexSafety::Ignore:
			return SanitizeIndex<T, EPCGExIndexSafety::Ignore>(Index, MaxIndex);
		case EPCGExIndexSafety::Tile:
			return SanitizeIndex<T, EPCGExIndexSafety::Tile>(Index, MaxIndex);
		case EPCGExIndexSafety::Clamp:
			return SanitizeIndex<T, EPCGExIndexSafety::Clamp>(Index, MaxIndex);
		case EPCGExIndexSafety::Yoyo:
			return SanitizeIndex<T, EPCGExIndexSafety::Yoyo>(Index, MaxIndex);
		}
	}

	PCGEXCORE_API void CheckConvex(const FVector& A, const FVector& B, const FVector& C, bool& bIsConvex, int32& OutSign, const FVector& UpVector = FVector::UpVector);
	PCGEXCORE_API FBox ScaledBox(const FBox& InBox, const FVector& InScale);
	PCGEXCORE_API bool IsDirectionWithinTolerance(const FVector& A, const FVector& B, const FRotator& Limits);

	PCGEXCORE_API double GetArcLength(const double R, const double StartAngleRadians, const double EndAngleRadians);

	/** Distance from C to AB */
	PCGEXCORE_API double GetPerpendicularDistance(const FVector& A, const FVector& B, const FVector& C);
}
