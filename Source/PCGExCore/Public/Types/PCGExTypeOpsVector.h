// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExTypeOps.h"

/**
 * Vector Type Operations
 * FVector2D, FVector, FVector4
 */

namespace PCGExTypeOps
{
	// Vector Type Operations - FVector2D

	template <>
	struct FTypeOps<FVector2D>
	{
		using Type = FVector2D;

		static FORCEINLINE Type GetDefault() { return FVector2D::ZeroVector; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value.SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value.X; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value.X, Value.Y, 0.0); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value.X, Value.Y, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value.X, Value.Y, 0.0).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value.X, Value.Y, 0.0); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform(FVector(Value.X, Value.Y, 0.0)); }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Type(Value ? 1.0 : 0.0); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(Value.X, Value.Y); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(Value.X, Value.Y); }
			else if constexpr (std::is_same_v<TFrom, FQuat>)
			{
				const FRotator R = Value.Rotator();
				return Type(R.Pitch, R.Yaw);
			}
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(Value.Pitch, Value.Yaw); }
			else if constexpr (std::is_same_v<TFrom, FTransform>)
			{
				const FVector L = Value.GetLocation();
				return Type(L.X, L.Y);
			}
			else if constexpr (std::is_same_v<TFrom, FString>)
			{
				Type Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FName>)
			{
				Type Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type::ZeroVector; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type::ZeroVector; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type::ZeroVector; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? A / D : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return FMath::Lerp(A, B, W); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return Type(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return Type(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) * 0.5; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + B * W; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - B * W; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) <= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) <= FMath::Abs(B.Y) ? A.Y : B.Y);
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) >= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) >= FMath::Abs(B.Y) ? A.Y : B.Y);
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Min(FMath::Abs(A.Y), FMath::Abs(B.Y)));
		}

		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Max(FMath::Abs(A.Y), FMath::Abs(B.Y)));
		}

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(A.X), GetTypeHash(B.X)),
				HashCombine(GetTypeHash(A.Y), GetTypeHash(B.Y)));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(FMath::Min(A.X, B.X)), GetTypeHash(FMath::Max(A.X, B.X))),
				HashCombine(GetTypeHash(FMath::Min(A.Y, B.Y)), GetTypeHash(FMath::Max(A.Y, B.Y))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			return M != 0.0 ? Type(FMath::Fmod(A.X, M), FMath::Fmod(A.Y, M)) : A;
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			return Type(
				B.X != 0.0 ? FMath::Fmod(A.X, B.X) : A.X,
				B.Y != 0.0 ? FMath::Fmod(A.Y, B.Y) : A.Y);
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? (A + B) / W : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return Type(FMath::Abs(A.X), FMath::Abs(A.Y)); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const Type& V = *static_cast<const Type*>(Value);
			switch (Field)
			{
			case ESingleField::X: return V.X;
			case ESingleField::Y: return V.Y;
			case ESingleField::Length: return V.Length();
			case ESingleField::SquaredLength: return V.SquaredLength();
			case ESingleField::Volume: return V.X * V.Y;
			case ESingleField::Sum: return V.X + V.Y;
			default: return V.X;
			}
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& V = *static_cast<Type*>(Target);
			switch (Field)
			{
			case ESingleField::X: V.X = Value;
				break;
			case ESingleField::Y: V.Y = Value;
				break;
			case ESingleField::Length: V = V.GetSafeNormal() * Value;
				break;
			case ESingleField::SquaredLength: V = V.GetSafeNormal() * FMath::Sqrt(Value);
				break;
			default: break;
			}
		}
	};

	// Vector Type Operations - FVector

	template <>
	struct FTypeOps<FVector>
	{
		using Type = FVector;

		static FORCEINLINE Type GetDefault() { return FVector::ZeroVector; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return Value.SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value.X; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value.X, Value.Y); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value.X, Value.Y, Value.Z, 0.0); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FRotator(Value.X, Value.Y, Value.Z).Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform(Value); }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Type(Value ? 1.0 : 0.0); }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(Value.X, Value.Y, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TFrom, FQuat>)
			{
				const FRotator R = Value.Rotator();
				return Type(R.Pitch, R.Yaw, R.Roll);
			}
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(Value.Pitch, Value.Yaw, Value.Roll); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Value.GetLocation(); }
			else if constexpr (std::is_same_v<TFrom, FString>)
			{
				Type Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FName>)
			{
				Type Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type::ZeroVector; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type::ZeroVector; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type::ZeroVector; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? A / D : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return FMath::Lerp(A, B, W); }
		static FORCEINLINE Type Min(const Type& A, const Type& B) { return A.ComponentMin(B); }
		static FORCEINLINE Type Max(const Type& A, const Type& B) { return A.ComponentMax(B); }
		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) * 0.5; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + B * W; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - B * W; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) <= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) <= FMath::Abs(B.Y) ? A.Y : B.Y,
				FMath::Abs(A.Z) <= FMath::Abs(B.Z) ? A.Z : B.Z);
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) >= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) >= FMath::Abs(B.Y) ? A.Y : B.Y,
				FMath::Abs(A.Z) >= FMath::Abs(B.Z) ? A.Z : B.Z);
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Min(FMath::Abs(A.Y), FMath::Abs(B.Y)),
				FMath::Min(FMath::Abs(A.Z), FMath::Abs(B.Z)));
		}

		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Max(FMath::Abs(A.Y), FMath::Abs(B.Y)),
				FMath::Max(FMath::Abs(A.Z), FMath::Abs(B.Z)));
		}

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(A.X), GetTypeHash(B.X)),
				HashCombine(GetTypeHash(A.Y), GetTypeHash(B.Y)),
				HashCombine(GetTypeHash(A.Z), GetTypeHash(B.Z)));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(FMath::Min(A.X, B.X)), GetTypeHash(FMath::Max(A.X, B.X))),
				HashCombine(GetTypeHash(FMath::Min(A.Y, B.Y)), GetTypeHash(FMath::Max(A.Y, B.Y))),
				HashCombine(GetTypeHash(FMath::Min(A.Z, B.Z)), GetTypeHash(FMath::Max(A.Z, B.Z))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			return M != 0.0 ? Type(FMath::Fmod(A.X, M), FMath::Fmod(A.Y, M), FMath::Fmod(A.Z, M)) : A;
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			return Type(
				B.X != 0.0 ? FMath::Fmod(A.X, B.X) : A.X,
				B.Y != 0.0 ? FMath::Fmod(A.Y, B.Y) : A.Y,
				B.Z != 0.0 ? FMath::Fmod(A.Z, B.Z) : A.Z);
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? (A + B) / W : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return Type(FMath::Abs(A.X), FMath::Abs(A.Y), FMath::Abs(A.Z)); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const Type& V = *static_cast<const Type*>(Value);
			switch (Field)
			{
			case ESingleField::X: return V.X;
			case ESingleField::Y: return V.Y;
			case ESingleField::Z: return V.Z;
			case ESingleField::Length: return V.Length();
			case ESingleField::SquaredLength: return V.SquaredLength();
			case ESingleField::Volume: return V.X * V.Y * V.Z;
			case ESingleField::Sum: return V.X + V.Y + V.Z;
			default: return V.X;
			}
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& V = *static_cast<Type*>(Target);
			switch (Field)
			{
			case ESingleField::X: V.X = Value;
				break;
			case ESingleField::Y: V.Y = Value;
				break;
			case ESingleField::Z: V.Z = Value;
				break;
			case ESingleField::Length: V = V.GetSafeNormal() * Value;
				break;
			case ESingleField::SquaredLength: V = V.GetSafeNormal() * FMath::Sqrt(Value);
				break;
			default: break;
			}
		}
	};

	// Vector Type Operations - FVector4

	template <>
	struct FTypeOps<FVector4>
	{
		using Type = FVector4;

		static FORCEINLINE Type GetDefault() { return FVector4(0.0, 0.0, 0.0, 0.0); }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return FVector(Value.X, Value.Y, Value.Z).SquaredLength() > 0.0; }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value.X); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value.X); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value.X); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value.X; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value.X, Value.Y); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return FQuat(Value.X, Value.Y, Value.Z, Value.W); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return FRotator(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform(FVector(Value.X, Value.Y, Value.Z)); }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value.X, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>)
			{
				const double D = Value ? 1.0 : 0.0;
				return Type(D, D, D, D);
			}
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(Value.X, Value.Y, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(Value.X, Value.Y, Value.Z, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Type(Value.X, Value.Y, Value.Z, Value.W); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(Value.Pitch, Value.Yaw, Value.Roll, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FTransform>)
			{
				const FVector L = Value.GetLocation();
				return Type(L.X, L.Y, L.Z, 0.0);
			}
			else if constexpr (std::is_same_v<TFrom, FString>)
			{
				Type Result;
				Result.InitFromString(Value);
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FName>)
			{
				Type Result;
				Result.InitFromString(Value.ToString());
				return Result;
			}
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type(0.0, 0.0, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type(0.0, 0.0, 0.0, 0.0); }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(Value, Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type(0.0, 0.0, 0.0, 0.0); }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }
		static FORCEINLINE Type Div(const Type& A, double D) { return D != 0.0 ? A / D : A; }
		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return A + (B - A) * W; }

		static FORCEINLINE Type Min(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y),
				FMath::Min(A.Z, B.Z), FMath::Min(A.W, B.W));
		}

		static FORCEINLINE Type Max(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y),
				FMath::Max(A.Z, B.Z), FMath::Max(A.W, B.W));
		}

		static FORCEINLINE Type Average(const Type& A, const Type& B) { return (A + B) * 0.5; }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return A + B * W; }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return A - B * W; }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) <= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) <= FMath::Abs(B.Y) ? A.Y : B.Y,
				FMath::Abs(A.Z) <= FMath::Abs(B.Z) ? A.Z : B.Z,
				FMath::Abs(A.W) <= FMath::Abs(B.W) ? A.W : B.W);
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.X) >= FMath::Abs(B.X) ? A.X : B.X,
				FMath::Abs(A.Y) >= FMath::Abs(B.Y) ? A.Y : B.Y,
				FMath::Abs(A.Z) >= FMath::Abs(B.Z) ? A.Z : B.Z,
				FMath::Abs(A.W) >= FMath::Abs(B.W) ? A.W : B.W);
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Min(FMath::Abs(A.Y), FMath::Abs(B.Y)),
				FMath::Min(FMath::Abs(A.Z), FMath::Abs(B.Z)),
				FMath::Min(FMath::Abs(A.W), FMath::Abs(B.W)));
		}

		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(FMath::Abs(A.X), FMath::Abs(B.X)),
				FMath::Max(FMath::Abs(A.Y), FMath::Abs(B.Y)),
				FMath::Max(FMath::Abs(A.Z), FMath::Abs(B.Z)),
				FMath::Max(FMath::Abs(A.W), FMath::Abs(B.W)));
		}

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(A.X), GetTypeHash(B.X)),
				HashCombine(GetTypeHash(A.Y), GetTypeHash(B.Y)),
				HashCombine(GetTypeHash(A.Z), GetTypeHash(B.Z)),
				HashCombine(GetTypeHash(A.W), GetTypeHash(B.W)));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(FMath::Min(A.X, B.X)), GetTypeHash(FMath::Max(A.X, B.X))),
				HashCombine(GetTypeHash(FMath::Min(A.Y, B.Y)), GetTypeHash(FMath::Max(A.Y, B.Y))),
				HashCombine(GetTypeHash(FMath::Min(A.Z, B.Z)), GetTypeHash(FMath::Max(A.Z, B.Z))),
				HashCombine(GetTypeHash(FMath::Min(A.W, B.W)), GetTypeHash(FMath::Max(A.W, B.W))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			return M != 0.0 ? Type(FMath::Fmod(A.X, M), FMath::Fmod(A.Y, M), FMath::Fmod(A.Z, M), FMath::Fmod(A.W, M)) : A;
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			return Type(
				B.X != 0.0 ? FMath::Fmod(A.X, B.X) : A.X,
				B.Y != 0.0 ? FMath::Fmod(A.Y, B.Y) : A.Y,
				B.Z != 0.0 ? FMath::Fmod(A.Z, B.Z) : A.Z,
				B.W != 0.0 ? FMath::Fmod(A.W, B.W) : A.W);
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W) { return W != 0.0 ? (A + B) / W : A; }

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return Type(FMath::Abs(A.X), FMath::Abs(A.Y), FMath::Abs(A.Z), FMath::Abs(A.W)); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return Type(A.X * Factor, A.Y * Factor, A.Z * Factor, A.W * Factor); }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const Type& V = *static_cast<const Type*>(Value);
			switch (Field)
			{
			case ESingleField::X: return V.X;
			case ESingleField::Y: return V.Y;
			case ESingleField::Z: return V.Z;
			case ESingleField::W: return V.W;
			case ESingleField::Length: return FVector(V.X, V.Y, V.Z).Length();
			case ESingleField::SquaredLength: return FVector(V.X, V.Y, V.Z).SquaredLength();
			case ESingleField::Volume: return V.X * V.Y * V.Z * V.W;
			case ESingleField::Sum: return V.X + V.Y + V.Z + V.W;
			default: return V.X;
			}
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& V = *static_cast<Type*>(Target);
			switch (Field)
			{
			case ESingleField::X: V.X = Value;
				break;
			case ESingleField::Y: V.Y = Value;
				break;
			case ESingleField::Z: V.Z = Value;
				break;
			case ESingleField::W: V.W = Value;
				break;
			case ESingleField::Length:
				{
					FVector Vec(V.X, V.Y, V.Z);
					Vec = Vec.GetSafeNormal() * Value;
					V = Type(Vec.X, Vec.Y, Vec.Z, V.W);
				}
				break;
			case ESingleField::SquaredLength:
				{
					FVector Vec(V.X, V.Y, V.Z);
					Vec = Vec.GetSafeNormal() * FMath::Sqrt(Value);
					V = Type(Vec.X, Vec.Y, Vec.Z, V.W);
				}
				break;
			default: break;
			}
		}
	};
}
