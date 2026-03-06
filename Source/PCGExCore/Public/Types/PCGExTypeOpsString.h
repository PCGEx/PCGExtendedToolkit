// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExTypeOps.h"
#include "UObject/SoftObjectPath.h"

/**
 * String and Path Type Operations
 * FString, FName, FSoftObjectPath, FSoftClassPath
 */

namespace PCGExTypeOps
{
	// String Type Operations - FString

	template <>
	struct FTypeOps<FString>
	{
		using Type = FString;

		static FORCEINLINE Type GetDefault() { return Type(); }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value.ToBool(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return FCString::Atoi(*Value); }
			else if constexpr (std::is_same_v<TTo, int64>) { return FCString::Atoi64(*Value); }
			else if constexpr (std::is_same_v<TTo, float>) { return FCString::Atof(*Value); }
			else if constexpr (std::is_same_v<TTo, double>) { return FCString::Atod(*Value); }
			else if constexpr (std::is_same_v<TTo, FVector2D>)
			{
				FVector2D Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector>)
			{
				FVector Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector4>)
			{
				FVector4 Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FQuat>)
			{
				FQuat Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FRotator>)
			{
				FRotator Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FTransform>)
			{
				FTransform Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FString>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(Value); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(Value); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Value), 0, 255)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Value ? TEXT("true") : TEXT("false"); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return FString::Printf(TEXT("%d"), Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return FString::Printf(TEXT("%lld"), Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return FString::Printf(TEXT("%f"), Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return FString::Printf(TEXT("%f"), Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FName>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return FString::Printf(TEXT("%d"), static_cast<int32>(Value)); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return Value.ToString(); }
			else { return Type(); }
		}

		// String blend operations - mostly concatenation-based
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A.Replace(*B, TEXT("")); }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.Len() <= B.Len() ? A : B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.Len() >= B.Len() ? A : B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return A + TEXT("|") + B; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 ? A + B : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return W > 0.5 ? A.Replace(*B, TEXT("")) : A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(A), GetTypeHash(B)));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinS = A < B ? A : B;
			const Type MaxS = A >= B ? A : B;
			return FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(MinS), GetTypeHash(MaxS)));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field) { return 0; }

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
		}
	};

	// Name Type Operations - FName

	template <>
	struct FTypeOps<FName>
	{
		using Type = FName;

		static FORCEINLINE Type GetDefault() { return NAME_None; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return !Value.IsNone(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return FCString::Atoi(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, int64>) { return FCString::Atoi64(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, float>) { return FCString::Atof(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, double>) { return FCString::Atod(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FVector2D>)
			{
				FVector2D Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector>)
			{
				FVector Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector4>)
			{
				FVector4 Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FQuat>)
			{
				FQuat Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FRotator>)
			{
				FRotator Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FTransform>)
			{
				FTransform Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Value.ToString()), 0, 255)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromName(Value); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return FName(Value ? TEXT("true") : TEXT("false")); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return FName(FString::Printf(TEXT("%d"), Value)); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return FName(FString::Printf(TEXT("%lld"), Value)); }
			else if constexpr (std::is_same_v<TFrom, float>) { return FName(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TFrom, double>) { return FName(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FName(*Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return FName(FString::Printf(TEXT("%d"), static_cast<int32>(Value))); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return FName(*Value.ToString()); }
			else { return NAME_None; }
		}

		// Name blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return FName(*(A.ToString() + B.ToString())); }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return FName(*A.ToString().Replace(*B.ToString(), TEXT(""))); }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return Add(A, B); }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.ToString().Len() <= B.ToString().Len() ? A : B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.ToString().Len() >= B.ToString().Len() ? A : B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return FName(*(A.ToString() + TEXT("_") + B.ToString())); }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 ? Add(A, B) : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return W > 0.5 ? Sub(A, B) : A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return FName(*FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(A), GetTypeHash(B))));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const Type MinN = A.Compare(B) <= 0 ? A : B;
			const Type MaxN = A.Compare(B) > 0 ? A : B;
			return FName(*FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(MinN), GetTypeHash(MaxN))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field) { return 0; }

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
		}
	};

	// Path Type Operations - FSoftObjectPath

	template <>
	struct FTypeOps<FSoftObjectPath>
	{
		using Type = FSoftObjectPath;

		static FORCEINLINE Type GetDefault() { return Type(); }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value.IsValid(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return 0; }
			else if constexpr (std::is_same_v<TTo, int64>) { return 0; }
			else if constexpr (std::is_same_v<TTo, float>) { return 0.0f; }
			else if constexpr (std::is_same_v<TTo, double>) { return 0.0; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D::ZeroVector; }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector::ZeroVector; }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(0.0, 0.0, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FQuat::Identity; }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator::ZeroRotator; }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return 0; }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return Type(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return Type(Value.ToString()); }
			else { return Type(); }
		}

		// Path blend operations - mostly selection-based
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A.IsValid() ? A : B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A.IsValid() && B.IsValid() ? A : Type(); }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.ToString() < B.ToString() ? A : B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.ToString() > B.ToString() ? A : B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return A.IsValid() ? A : B; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 && B.IsValid() ? B : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field) { return 0; }

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
		}
	};

	// Path Type Operations - FSoftClassPath

	template <>
	struct FTypeOps<FSoftClassPath>
	{
		using Type = FSoftClassPath;

		static FORCEINLINE Type GetDefault() { return Type(); }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value.IsValid(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return 0; }
			else if constexpr (std::is_same_v<TTo, int64>) { return 0; }
			else if constexpr (std::is_same_v<TTo, float>) { return 0.0f; }
			else if constexpr (std::is_same_v<TTo, double>) { return 0.0; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D::ZeroVector; }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector::ZeroVector; }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(0.0, 0.0, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FQuat::Identity; }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator::ZeroRotator; }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform::Identity; }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return Value; }
			else if constexpr (std::is_same_v<TTo, uint8>) { return 0; }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return Type(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return Type(Value.ToString()); }
			else { return Type(); }
		}

		// Path blend operations - same as FSoftObjectPath
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A.IsValid() ? A : B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A.IsValid() && B.IsValid() ? A : Type(); }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.ToString() < B.ToString() ? A : B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.ToString() > B.ToString() ? A : B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return A.IsValid() ? A : B; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 && B.IsValid() ? B : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field) { return 0; }

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
		}
	};

	// Text Type Operations - FText

	template <>
	struct FTypeOps<FText>
	{
		using Type = FText;

		static FORCEINLINE Type GetDefault() { return FText::GetEmpty(); }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value.ToString()); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			const FString Str = Value.ToString();
			if constexpr (std::is_same_v<TTo, bool>) { return !Value.IsEmpty(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return FCString::Atoi(*Str); }
			else if constexpr (std::is_same_v<TTo, int64>) { return FCString::Atoi64(*Str); }
			else if constexpr (std::is_same_v<TTo, float>) { return FCString::Atof(*Str); }
			else if constexpr (std::is_same_v<TTo, double>) { return FCString::Atod(*Str); }
			else if constexpr (std::is_same_v<TTo, FVector2D>)
			{
				FVector2D Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector>)
			{
				FVector Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FVector4>)
			{
				FVector4 Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FQuat>)
			{
				FQuat Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FRotator>)
			{
				FRotator Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FTransform>)
			{
				FTransform Result;
				Result.InitFromString(Str);
				return Result;
			}
			else if constexpr (std::is_same_v<TTo, FString>) { return Str; }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Str); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(Str); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(Str); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Str), 0, 255)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return Value; }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return FText::FromString(Value ? TEXT("true") : TEXT("false")); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return FText::FromString(FString::Printf(TEXT("%d"), Value)); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return FText::FromString(FString::Printf(TEXT("%lld"), Value)); }
			else if constexpr (std::is_same_v<TFrom, float>) { return FText::FromString(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TFrom, double>) { return FText::FromString(FString::Printf(TEXT("%f"), Value)); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FString>) { return FText::FromString(Value); }
			else if constexpr (std::is_same_v<TFrom, FName>) { return FText::FromName(Value); }
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return FText::FromString(Value.ToString()); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return FText::FromString(FString::Printf(TEXT("%d"), static_cast<int32>(Value))); }
			else if constexpr (std::is_same_v<TFrom, FText>) { return Value; }
			else { return FText::GetEmpty(); }
		}

		// Text blend operations - mirroring FString semantics
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return FText::FromString(A.ToString() + B.ToString()); }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return FText::FromString(A.ToString().Replace(*B.ToString(), TEXT(""))); }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return Add(A, B); }
		static FORCEINLINE Type Div(const Type& A, double D) { return A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.ToString().Len() <= B.ToString().Len() ? A : B; }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.ToString().Len() >= B.ToString().Len() ? A : B; }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return FText::FromString(A.ToString() + TEXT("|") + B.ToString()); }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return W > 0.5 ? Add(A, B) : A; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return W > 0.5 ? Sub(A, B) : A; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }
		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return FText::FromString(FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(A.ToString()), GetTypeHash(B.ToString()))));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			const FString SA = A.ToString(), SB = B.ToString();
			const FString MinS = SA < SB ? SA : SB;
			const FString MaxS = SA >= SB ? SA : SB;
			return FText::FromString(FString::Printf(TEXT("%u"), HashCombine(GetTypeHash(MinS), GetTypeHash(MaxS))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M) { return A; }
		static FORCEINLINE Type ModComplex(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W < 0.5 ? A : B; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A; }

		static FORCEINLINE Type Abs(const Type& A) { return A; }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field) { return 0; }

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
		}
	};
}
