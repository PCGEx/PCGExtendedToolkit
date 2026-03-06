// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExTypeOps.h"

/**
 * Per-Type Operations Implementation
 * 
 * Each FTypeOps<T> contains ALL operations for type T in one place:
 * - Conversion TO other types (ConvertTo<Target>)
 * - Conversion FROM other types (ConvertFrom<Source>)
 * - All blend operations
 * - Hash computation
 * 
 * This centralizes maintenance: to add/modify behavior for a type
 */

namespace PCGExTypeOps
{
	// Forward Declarations for Cross-Type Conversion

	// Forward declare all FTypeOps specializations so they can reference each other
	template <>
	struct FTypeOps<bool>;
	template <>
	struct FTypeOps<int32>;
	template <>
	struct FTypeOps<int64>;
	template <>
	struct FTypeOps<float>;
	template <>
	struct FTypeOps<double>;
	template <>
	struct FTypeOps<FVector2D>;
	template <>
	struct FTypeOps<FVector>;
	template <>
	struct FTypeOps<FVector4>;
	template <>
	struct FTypeOps<FQuat>;
	template <>
	struct FTypeOps<FRotator>;
	template <>
	struct FTypeOps<FTransform>;
	template <>
	struct FTypeOps<FString>;
	template <>
	struct FTypeOps<FName>;
	template <>
	struct FTypeOps<FSoftObjectPath>;
	template <>
	struct FTypeOps<FSoftClassPath>;
	template <>
	struct FTypeOps<uint8>;
	template <>
	struct FTypeOps<FText>;

	// Numeric Type Operations - bool

	template <>
	struct FTypeOps<bool>
	{
		using Type = bool;

		// Default value
		static FORCEINLINE Type GetDefault() { return false; }

		// Hash
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		// Conversions TO other types

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value; }
			else if constexpr (std::is_same_v<TTo, int32>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TTo, int64>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TTo, float>) { return Value ? 1.0f : 0.0f; }
			else if constexpr (std::is_same_v<TTo, double>) { return Value ? 1.0 : 0.0; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value ? 1.0 : 0.0); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value ? 1.0 : 0.0); }
			else if constexpr (std::is_same_v<TTo, FVector4>)
			{
				const double D = Value ? 1.0 : 0.0;
				return FVector4(D, D, D, D);
			}
			else if constexpr (std::is_same_v<TTo, FQuat>)
			{
				const double D = Value ? 180.0 : 0.0;
				return FRotator(D, D, D).Quaternion();
			}
			else if constexpr (std::is_same_v<TTo, FRotator>)
			{
				const double D = Value ? 180.0 : 0.0;
				return FRotator(D, D, D);
			}
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value ? TEXT("true") : TEXT("false"); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(Value ? TEXT("true") : TEXT("false")); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value ? TEXT("true") : TEXT("false")); }
			else { return TTo{}; }
		}

		// Conversions FROM other types

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Value > 0; }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Value > 0; }
			else if constexpr (std::is_same_v<TFrom, float>) { return Value > 0.0f; }
			else if constexpr (std::is_same_v<TFrom, double>) { return Value > 0.0; }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Value.SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Value.SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return FVector(Value.X, Value.Y, Value.Z).SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return !Value.IsIdentity(); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return !Value.IsNearlyZero(); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return !Value.Equals(FTransform::Identity); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return Value.ToBool(); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return !Value.IsNone(); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Value.IsValid(); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Value.IsValid(); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Value > 0; }
			else if constexpr (std::is_same_v<TFrom, FText>) { return !Value.IsEmpty(); }
			else { return false; }
		}

		// Blend Operations

		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A || B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A && !B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A && B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A && B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A || B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return A || B; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 ? (A || B) : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return W > 0.5 ? (A && !B) : A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return A && B; }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return A || B; }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return A && B; }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return A || B; }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return A != B; }
		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B) { return A != B; }
		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W > 0.5 ? B : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return FMath::IsNearlyZero(Factor) ? false : A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return *static_cast<const Type*>(Value);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = static_cast<Type>(Value);
		}
	};

	// Numeric Type Operations - int32

	template <>
	struct FTypeOps<int32>
	{
		using Type = int32;

		static FORCEINLINE Type GetDefault() { return 0; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value > 0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return Value; }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TTo, double>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return FString::Printf(TEXT("%d"), Value); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(FString::Printf(TEXT("%d"), Value)); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value, 0, 255)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(FString::Printf(TEXT("%d"), Value)); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, int64>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return static_cast<int32>(Value.W); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return static_cast<int32>(Value.Pitch); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FCString::Atoi(*Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return FCString::Atoi(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return FCString::Atoi(*Value.ToString()); }
			else { return 0; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? static_cast<Type>(A / D) : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return static_cast<Type>(FMath::Lerp(static_cast<double>(A), static_cast<double>(B), W)); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) / 2; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + static_cast<Type>(B * W); }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - static_cast<Type>(B * W); }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA <= AbsB ? A : B;
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA >= AbsB ? A : B;
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return FMath::Min(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return FMath::Max(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return static_cast<Type>(HashCombine(GetTypeHash(A), GetTypeHash(B))); }

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinV = FMath::Min(A, B), MaxV = FMath::Max(A, B);
			return static_cast<Type>(HashCombine(GetTypeHash(MinV), GetTypeHash(MaxV)));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return M != 0.0 ? static_cast<Type>(FMath::Fmod(static_cast<double>(A), M)) : A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return B != 0 ? A % B : A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? static_cast<Type>((A + B) / W) : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return FMath::Abs(A); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return *static_cast<const Type*>(Value);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = static_cast<Type>(Value);
		}
	};

	// Numeric Type Operations - int64

	template <>
	struct FTypeOps<int64>
	{
		using Type = int64;

		static FORCEINLINE Type GetDefault() { return 0; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value > 0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TTo, int64>) { return Value; }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TTo, double>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return FString::Printf(TEXT("%lld"), Value); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(FString::Printf(TEXT("%lld"), Value)); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value, static_cast<int64>(0), static_cast<int64>(255))); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(FString::Printf(TEXT("%lld"), Value)); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, float>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return static_cast<int64>(Value.W); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return static_cast<int64>(Value.Pitch); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FCString::Atoi64(*Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return FCString::Atoi64(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return FCString::Atoi64(*Value.ToString()); }
			else { return 0; }
		}

		// Blend operations (same pattern as int32)
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? static_cast<Type>(A / D) : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return static_cast<Type>(FMath::Lerp(static_cast<double>(A), static_cast<double>(B), W)); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) / 2; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + static_cast<Type>(B * W); }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - static_cast<Type>(B * W); }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA <= AbsB ? A : B;
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA >= AbsB ? A : B;
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return FMath::Min(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return FMath::Max(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return HashCombine(GetTypeHash(A), GetTypeHash(B)); }

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinV = FMath::Min(A, B), MaxV = FMath::Max(A, B);
			return HashCombine(GetTypeHash(MinV), GetTypeHash(MaxV));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return M != 0.0 ? static_cast<Type>(FMath::Fmod(static_cast<double>(A), M)) : A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return B != 0 ? A % B : A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? static_cast<Type>((A + B) / W) : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return FMath::Abs(A); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return static_cast<double>(*static_cast<const Type*>(Value));
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = static_cast<Type>(Value);
		}
	};

	// Numeric Type Operations - float

	template <>
	struct FTypeOps<float>
	{
		using Type = float;

		static FORCEINLINE Type GetDefault() { return 0.0f; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value > 0.0f; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TTo, float>) { return Value; }
			else if constexpr (std::is_same_v<TTo, double>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return FString::Printf(TEXT("%f"), Value); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value, 0.0f, 255.0f)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(FString::Printf(TEXT("%f"), Value)); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? 1.0f : 0.0f; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, double>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return static_cast<float>(Value.W); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return static_cast<float>(Value.Pitch); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return 0.0f; }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FCString::Atof(*Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return FCString::Atof(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return 0.0f; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return 0.0f; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return FCString::Atof(*Value.ToString()); }
			else { return 0.0f; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? static_cast<Type>(A / D) : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return FMath::Lerp(A, B, static_cast<float>(W)); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) * 0.5f; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + static_cast<Type>(B * W); }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - static_cast<Type>(B * W); }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA <= AbsB ? A : B;
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA >= AbsB ? A : B;
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return FMath::Min(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return FMath::Max(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return static_cast<Type>(HashCombine(GetTypeHash(A), GetTypeHash(B))); }

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinV = FMath::Min(A, B), MaxV = FMath::Max(A, B);
			return static_cast<Type>(HashCombine(GetTypeHash(MinV), GetTypeHash(MaxV)));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return M != 0.0 ? FMath::Fmod(A, static_cast<float>(M)) : A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return B != 0.0f ? FMath::Fmod(A, B) : A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? static_cast<Type>((A + B) / W) : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return FMath::Abs(A); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return *static_cast<const Type*>(Value);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = static_cast<Type>(Value);
		}
	};

	// Numeric Type Operations - double

	template <>
	struct FTypeOps<double>
	{
		using Type = double;

		static FORCEINLINE Type GetDefault() { return 0.0; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value > 0.0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return FString::Printf(TEXT("%f"), Value); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(FString::Printf(TEXT("%f"), Value)); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? 1.0 : 0.0; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Value.X; }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Value.X; }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Value.X; }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Value.W; }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Value.Pitch; }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return 0.0; }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FCString::Atod(*Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return FCString::Atod(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return 0.0; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return 0.0; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return FCString::Atod(*Value.ToString()); }
			else { return 0.0; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? A / D : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return FMath::Lerp(A, B, W); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) * 0.5; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + B * W; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - B * W; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA <= AbsB ? A : B;
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			const Type AbsA = FMath::Abs(A), AbsB = FMath::Abs(B);
			return AbsA >= AbsB ? A : B;
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return FMath::Min(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return FMath::Max(FMath::Abs(A), FMath::Abs(B)); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return HashCombine(GetTypeHash(A), GetTypeHash(B)); }

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinV = FMath::Min(A, B), MaxV = FMath::Max(A, B);
			return HashCombine(GetTypeHash(MinV), GetTypeHash(MaxV));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return M != 0.0 ? FMath::Fmod(A, M) : A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return B != 0.0 ? FMath::Fmod(A, B) : A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? (A + B) / W : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return FMath::Abs(A); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return *static_cast<const Type*>(Value);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = Value;
		}
	};

	// Numeric Type Operations - uint8 (Byte)

	template <>
	struct FTypeOps<uint8>
	{
		using Type = uint8;

		static FORCEINLINE Type GetDefault() { return 0; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value > 0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value); }
			else if constexpr (std::is_same_v<TTo, double>) { return static_cast<double>(Value); }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value, Value, Value); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return FString::Printf(TEXT("%d"), static_cast<int32>(Value)); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(FString::Printf(TEXT("%d"), static_cast<int32>(Value))); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(FString::Printf(TEXT("%d"), static_cast<int32>(Value))); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? 1 : 0; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return static_cast<uint8>(FMath::Clamp(Value, 0, 255)); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return static_cast<uint8>(FMath::Clamp(Value, static_cast<int64>(0), static_cast<int64>(255))); }
			else if constexpr (std::is_same_v<TFrom, float>) { return static_cast<uint8>(FMath::Clamp(Value, 0.0f, 255.0f)); }
			else if constexpr (std::is_same_v<TFrom, double>) { return static_cast<uint8>(FMath::Clamp(Value, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return static_cast<uint8>(FMath::Clamp(Value.W * 255.0, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return static_cast<uint8>(FMath::Clamp(Value.Pitch, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FString>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Value), 0, 255)); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Value.ToString()), 0, 255)); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return 0; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FText>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Value.ToString()), 0, 255)); }
			else { return 0; }
		}

		// Blend operations - clamped to [0, 255]
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return static_cast<Type>(FMath::Min(static_cast<int32>(A) + B, 255)); }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return static_cast<Type>(FMath::Max(static_cast<int32>(A) - B, 0)); }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return static_cast<Type>(FMath::Clamp(static_cast<int32>(A) * B, 0, 255)); }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? static_cast<Type>(FMath::Clamp(static_cast<int32>(A / D), 0, 255)) : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return static_cast<Type>(FMath::Clamp(FMath::Lerp(static_cast<double>(A), static_cast<double>(B), W), 0.0, 255.0)); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return static_cast<Type>((static_cast<int32>(A) + B) / 2); }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return static_cast<Type>(FMath::Clamp(static_cast<int32>(A + B * W), 0, 255)); }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return static_cast<Type>(FMath::Clamp(static_cast<int32>(A - B * W), 0, 255)); }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return FMath::Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return FMath::Max(A, B); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return static_cast<Type>(HashCombine(GetTypeHash(A), GetTypeHash(B)) & 0xFF); }
		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinV = FMath::Min(A, B), MaxV = FMath::Max(A, B);
			return static_cast<Type>(HashCombine(GetTypeHash(MinV), GetTypeHash(MaxV)) & 0xFF);
		}
		static FORCEINLINE Type ModSimple(const Type& A, double M) { return M != 0.0 ? static_cast<Type>(FMath::Fmod(static_cast<double>(A), M)) : A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return B != 0 ? A % B : A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? static_cast<Type>(FMath::Clamp(static_cast<int32>((A + B) / W), 0, 255)) : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? static_cast<Type>(FMath::Clamp(A * (1.0 / TW), 0.0, 255.0)) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return static_cast<Type>(FMath::Clamp(A * Factor, 0.0, 255.0)); }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			return *static_cast<const Type*>(Value);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			*static_cast<Type*>(Target) = static_cast<Type>(FMath::Clamp(Value, 0.0, 255.0));
		}
	};
}
