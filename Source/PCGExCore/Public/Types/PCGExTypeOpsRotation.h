// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExTypeOps.h"
#include "PCGExTypeOpsVector.h"
#include "Math/PCGExMathAxis.h"

/**
 * Rotation and Transform Type Operations
 * FQuat, FRotator, FTransform
 */

namespace PCGExTypeOps
{
	// Rotation Type Operations - FQuat


	// Rotation Type Operations - FRotator

	template <>
	struct FTypeOps<FRotator>
	{
		using Type = FRotator;

		static FORCEINLINE Type GetDefault() { return FRotator::ZeroRotator; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value.Euler()); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return !Value.IsNearlyZero(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value.Pitch); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value.Pitch); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value.Pitch); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value.Pitch; }
			else if constexpr (std::is_same_v<TTo, FVector2D>) { return FVector2D(Value.Pitch, Value.Yaw); }
			else if constexpr (std::is_same_v<TTo, FVector>) { return FVector(Value.Pitch, Value.Yaw, Value.Roll); }
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value.Pitch, Value.Yaw, Value.Roll, 0.0); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return Value.Quaternion(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform(Value.Quaternion()); }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value.Pitch, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>)
			{
				const double D = Value ? 180.0 : 0.0;
				return Type(D, D, D);
			}
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type(Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type(Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type(Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type(Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(Value.X, Value.Y, 0.0); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(Value.X, Value.Y, Value.Z); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Value.Rotator(); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Value.Rotator(); }
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
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type::ZeroRotator; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type::ZeroRotator; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type(Value, Value, Value); }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type::ZeroRotator; }
		}

		// Blend operations
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return A + B; }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return A - B; }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return Type(A.Pitch * B.Pitch, A.Yaw * B.Yaw, A.Roll * B.Roll); }

		static FORCEINLINE Type Div(const Type& A, double D)
		{
			return D != 0.0 ? Type(A.Pitch / D, A.Yaw / D, A.Roll / D) : A;
		}

		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W)
		{
			return Type(
				FMath::Lerp(A.Pitch, B.Pitch, W),
				FMath::Lerp(A.Yaw, B.Yaw, W),
				FMath::Lerp(A.Roll, B.Roll, W));
		}

		static FORCEINLINE Type Min(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(A.Pitch, B.Pitch),
				FMath::Min(A.Yaw, B.Yaw),
				FMath::Min(A.Roll, B.Roll));
		}

		static FORCEINLINE Type Max(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(A.Pitch, B.Pitch),
				FMath::Max(A.Yaw, B.Yaw),
				FMath::Max(A.Roll, B.Roll));
		}

		static FORCEINLINE Type Average(const Type& A, const Type& B)
		{
			return Type(
				(A.Pitch + B.Pitch) * 0.5,
				(A.Yaw + B.Yaw) * 0.5,
				(A.Roll + B.Roll) * 0.5);
		}

		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W)
		{
			return Type(
				A.Pitch + B.Pitch * W,
				A.Yaw + B.Yaw * W,
				A.Roll + B.Roll * W);
		}

		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W)
		{
			return Type(
				A.Pitch - B.Pitch * W,
				A.Yaw - B.Yaw * W,
				A.Roll - B.Roll * W);
		}

		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.Pitch) <= FMath::Abs(B.Pitch) ? A.Pitch : B.Pitch,
				FMath::Abs(A.Yaw) <= FMath::Abs(B.Yaw) ? A.Yaw : B.Yaw,
				FMath::Abs(A.Roll) <= FMath::Abs(B.Roll) ? A.Roll : B.Roll);
		}

		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Abs(A.Pitch) >= FMath::Abs(B.Pitch) ? A.Pitch : B.Pitch,
				FMath::Abs(A.Yaw) >= FMath::Abs(B.Yaw) ? A.Yaw : B.Yaw,
				FMath::Abs(A.Roll) >= FMath::Abs(B.Roll) ? A.Roll : B.Roll);
		}

		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B)
		{
			return Type(
				FMath::Min(FMath::Abs(A.Pitch), FMath::Abs(B.Pitch)),
				FMath::Min(FMath::Abs(A.Yaw), FMath::Abs(B.Yaw)),
				FMath::Min(FMath::Abs(A.Roll), FMath::Abs(B.Roll)));
		}

		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B)
		{
			return Type(
				FMath::Max(FMath::Abs(A.Pitch), FMath::Abs(B.Pitch)),
				FMath::Max(FMath::Abs(A.Yaw), FMath::Abs(B.Yaw)),
				FMath::Max(FMath::Abs(A.Roll), FMath::Abs(B.Roll)));
		}

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(A.Pitch), GetTypeHash(B.Pitch)),
				HashCombine(GetTypeHash(A.Yaw), GetTypeHash(B.Yaw)),
				HashCombine(GetTypeHash(A.Roll), GetTypeHash(B.Roll)));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B)
		{
			return Type(
				HashCombine(GetTypeHash(FMath::Min(A.Pitch, B.Pitch)), GetTypeHash(FMath::Max(A.Pitch, B.Pitch))),
				HashCombine(GetTypeHash(FMath::Min(A.Yaw, B.Yaw)), GetTypeHash(FMath::Max(A.Yaw, B.Yaw))),
				HashCombine(GetTypeHash(FMath::Min(A.Roll, B.Roll)), GetTypeHash(FMath::Max(A.Roll, B.Roll))));
		}

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			return M != 0.0 ? Type(FMath::Fmod(A.Pitch, M), FMath::Fmod(A.Yaw, M), FMath::Fmod(A.Roll, M)) : A;
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			return Type(
				B.Pitch != 0.0 ? FMath::Fmod(A.Pitch, B.Pitch) : A.Pitch,
				B.Yaw != 0.0 ? FMath::Fmod(A.Yaw, B.Yaw) : A.Yaw,
				B.Roll != 0.0 ? FMath::Fmod(A.Roll, B.Roll) : A.Roll);
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W)
		{
			return W != 0.0 ? Div(Add(A, B), W) : A;
		}

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return TW != 0.0 ? A * (1.0 / TW) : A; }

		static FORCEINLINE Type Abs(const Type& A) { return Type(FMath::Abs(A.Pitch), FMath::Abs(A.Yaw), FMath::Abs(A.Roll)); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return A * Factor; }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const Type& R = *static_cast<const Type*>(Value);
			switch (Field)
			{
			case ESingleField::X: return R.Roll;
			case ESingleField::Y: return R.Yaw;
			case ESingleField::Z: return R.Pitch;
			default: return R.Roll;
			}
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& R = *static_cast<Type*>(Target);
			switch (Field)
			{
			case ESingleField::X: R.Roll = Value;
				break;
			case ESingleField::Y: R.Yaw = Value;
				break;
			case ESingleField::Z: R.Pitch = Value;
				break;
			case ESingleField::Length: R = R.GetNormalized() * Value;
				break;
			case ESingleField::SquaredLength: R = R.GetNormalized() * FMath::Sqrt(Value);
				break;
			default: break;
			}
		}

		static FORCEINLINE FVector ExtractAxis(const void* Value, EPCGExAxis Axis)
		{
			return PCGExMath::GetDirection(static_cast<const Type*>(Value)->Quaternion(), Axis);
		}
	};

	template <>
	struct FTypeOps<FQuat>
	{
		using Type = FQuat;

		static FORCEINLINE Type GetDefault() { return FQuat::Identity; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return !Value.IsIdentity(); }
			else if constexpr (std::is_same_v<TTo, int32>) { return static_cast<int32>(Value.W); }
			else if constexpr (std::is_same_v<TTo, int64>) { return static_cast<int64>(Value.W); }
			else if constexpr (std::is_same_v<TTo, float>) { return static_cast<float>(Value.W); }
			else if constexpr (std::is_same_v<TTo, double>) { return Value.W; }
			else if constexpr (std::is_same_v<TTo, FVector2D>)
			{
				const FRotator R = Value.Rotator();
				return FVector2D(R.Pitch, R.Yaw);
			}
			else if constexpr (std::is_same_v<TTo, FVector>)
			{
				const FRotator R = Value.Rotator();
				return FVector(R.Pitch, R.Yaw, R.Roll);
			}
			else if constexpr (std::is_same_v<TTo, FVector4>) { return FVector4(Value.X, Value.Y, Value.Z, Value.W); }
			else if constexpr (std::is_same_v<TTo, FQuat>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return Value.Rotator(); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return FTransform(Value); }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return static_cast<uint8>(FMath::Clamp(Value.W * 255.0, 0.0, 255.0)); }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>)
			{
				const double D = Value ? 180.0 : 0.0;
				return FRotator(D, D, D).Quaternion();
			}
			else if constexpr (std::is_same_v<TFrom, int32>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, int64>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, float>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, double>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return FRotator(Value.X, Value.Y, 0.0).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return FRotator(Value.X, Value.Y, Value.Z).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(Value.X, Value.Y, Value.Z, Value.W).GetNormalized(); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Value; }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Value.Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Value.GetRotation(); }
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
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return FRotator(Value, Value, Value).Quaternion(); }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type::Identity; }
		}

		// Blend operations - Quaternions require special handling
		static FORCEINLINE Type Add(const Type& A, const Type& B) { return FTypeOps<FRotator>::Add(A.Rotator(), B.Rotator()).Quaternion(); }
		static FORCEINLINE Type Sub(const Type& A, const Type& B) { return FTypeOps<FRotator>::Sub(A.Rotator(), B.Rotator()).Quaternion(); }
		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return (A * B).GetNormalized(); }

		static FORCEINLINE Type Div(const Type& A, double D)
		{
			if (D == 0.0)
			{
				return A;
			}
			// Convert to rotator, divide, convert back
			FRotator R = A.Rotator();
			R.Pitch /= D;
			R.Yaw /= D;
			R.Roll /= D;
			return R.Quaternion();
		}

		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W) { return FQuat::Slerp(A, B, W); }

		// Min/Max don't make much sense for quaternions - use angle-based comparison
		static FORCEINLINE Type Min(const Type& A, const Type& B)
		{
			return A.GetAngle() <= B.GetAngle() ? A : B;
		}

		static FORCEINLINE Type Max(const Type& A, const Type& B)
		{
			return A.GetAngle() >= B.GetAngle() ? A : B;
		}

		static FORCEINLINE Type Average(const Type& A, const Type& B) { return FQuat::Slerp(A, B, 0.5); }
		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W) { return FQuat::Slerp(A, A * B, W); }
		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W) { return FQuat::Slerp(A, A * B.Inverse(), W); }
		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			const uint32 H = HashCombine(GetTypeHash(A), GetTypeHash(B));
			return FQuat(H, H, H, 1.0).GetNormalized();
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B) { return NaiveHash(A, B); }

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			if (M == 0.0)
			{
				return A;
			}
			FRotator R = A.Rotator();
			R.Pitch = FMath::Fmod(R.Pitch, M);
			R.Yaw = FMath::Fmod(R.Yaw, M);
			R.Roll = FMath::Fmod(R.Roll, M);
			return R.Quaternion();
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			const FRotator RA = A.Rotator();
			const FRotator RB = B.Rotator();
			FRotator Result;
			Result.Pitch = RB.Pitch != 0.0 ? FMath::Fmod(RA.Pitch, RB.Pitch) : RA.Pitch;
			Result.Yaw = RB.Yaw != 0.0 ? FMath::Fmod(RA.Yaw, RB.Yaw) : RA.Yaw;
			Result.Roll = RB.Roll != 0.0 ? FMath::Fmod(RA.Roll, RB.Roll) : RA.Roll;
			return Result.Quaternion();
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W)
		{
			return W != 0.0 ? Div(A * B, W) : A;
		}

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW) { return A.GetNormalized(); }

		static FORCEINLINE Type Abs(const Type& A) { return FTypeOps<FRotator>::Abs(A.Rotator()).Quaternion().GetNormalized(); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return (A.Rotator() * Factor).Quaternion(); }

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const FRotator R = static_cast<const FQuat*>(Value)->Rotator();
			return FTypeOps<FRotator>::ExtractField(&R, Field);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& Q = *static_cast<Type*>(Target);
			FRotator R = Q.Rotator();
			FTypeOps<FRotator>::InjectField(&R, Value, Field);
			Q = R.Quaternion();
		}

		static FORCEINLINE FVector ExtractAxis(const void* Value, EPCGExAxis Axis)
		{
			return PCGExMath::GetDirection(*static_cast<const Type*>(Value), Axis);
		}
	};

	// Transform Type Operations - FTransform

	template <>
	struct FTypeOps<FTransform>
	{
		using Type = FTransform;

		static FORCEINLINE Type GetDefault() { return FTransform::Identity; }
		static FORCEINLINE PCGExValueHash Hash(const Type& Value) { return GetTypeHash(Value); }

		template <typename TTo>
		static TTo ConvertTo(const Type& Value)
		{
			if constexpr (std::is_same_v<TTo, bool>) { return !Value.Equals(FTransform::Identity); }
			else if constexpr (std::is_same_v<TTo, int32>) { return 0; }
			else if constexpr (std::is_same_v<TTo, int64>) { return 0; }
			else if constexpr (std::is_same_v<TTo, float>) { return 0.0f; }
			else if constexpr (std::is_same_v<TTo, double>) { return 0.0; }
			else if constexpr (std::is_same_v<TTo, FVector2D>)
			{
				const FVector L = Value.GetLocation();
				return FVector2D(L.X, L.Y);
			}
			else if constexpr (std::is_same_v<TTo, FVector>) { return Value.GetLocation(); }
			else if constexpr (std::is_same_v<TTo, FVector4>)
			{
				const FVector L = Value.GetLocation();
				return FVector4(L.X, L.Y, L.Z, 0.0);
			}
			else if constexpr (std::is_same_v<TTo, FQuat>) { return Value.GetRotation(); }
			else if constexpr (std::is_same_v<TTo, FRotator>) { return Value.Rotator(); }
			else if constexpr (std::is_same_v<TTo, FTransform>) { return Value; }
			else if constexpr (std::is_same_v<TTo, FString>) { return Value.ToString(); }
			else if constexpr (std::is_same_v<TTo, FName>) { return FName(*Value.ToString()); }
			else if constexpr (std::is_same_v<TTo, FSoftObjectPath>) { return FSoftObjectPath(); }
			else if constexpr (std::is_same_v<TTo, FSoftClassPath>) { return FSoftClassPath(); }
			else if constexpr (std::is_same_v<TTo, uint8>) { return 0; }
			else if constexpr (std::is_same_v<TTo, FText>) { return FText::FromString(Value.ToString()); }
			else { return TTo{}; }
		}

		template <typename TFrom>
		static Type ConvertFrom(const TFrom& Value)
		{
			if constexpr (std::is_same_v<TFrom, bool>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, int32>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, int64>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, float>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, double>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, FVector2D>) { return Type(FVector(Value.X, Value.Y, 0.0)); }
			else if constexpr (std::is_same_v<TFrom, FVector>) { return Type(Value); }
			else if constexpr (std::is_same_v<TFrom, FVector4>) { return Type(FVector(Value.X, Value.Y, Value.Z)); }
			else if constexpr (std::is_same_v<TFrom, FQuat>) { return Type(Value, FVector::ZeroVector, FVector::OneVector); }
			else if constexpr (std::is_same_v<TFrom, FRotator>) { return Type(Value.Quaternion(), FVector::ZeroVector, FVector::OneVector); }
			else if constexpr (std::is_same_v<TFrom, FTransform>) { return Value; }
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
			else if constexpr (std::is_same_v<TFrom, FSoftObjectPath>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, FSoftClassPath>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, uint8>) { return Type::Identity; }
			else if constexpr (std::is_same_v<TFrom, FText>) { Type Result; Result.InitFromString(Value.ToString()); return Result; }
			else { return Type::Identity; }
		}

		// Blend operations for transforms
		static FORCEINLINE Type Add(const Type& A, const Type& B)
		{
			return Type(
				A.GetRotation() * B.GetRotation(),
				A.GetLocation() + B.GetLocation(),
				A.GetScale3D() + B.GetScale3D());
		}

		static FORCEINLINE Type Sub(const Type& A, const Type& B)
		{
			return Type(
				A.GetRotation() * B.GetRotation().Inverse(),
				A.GetLocation() - B.GetLocation(),
				A.GetScale3D() - B.GetScale3D());
		}

		static FORCEINLINE Type Mult(const Type& A, const Type& B) { return A * B; }

		static FORCEINLINE Type Div(const Type& A, double D)
		{
			if (D == 0.0)
			{
				return A;
			}

			// Divide rotation via rotator
			FRotator R = A.Rotator();
			R.Pitch /= D;
			R.Yaw /= D;
			R.Roll /= D;

			return Type(
				R.Quaternion(),
				A.GetLocation() / D,
				A.GetScale3D() / D);
		}

		static FORCEINLINE Type Lerp(const Type& A, const Type& B, double W)
		{
			Type Result;
			Result.Blend(A, B, W);
			return Result;
		}

		static FORCEINLINE Type Min(const Type& A, const Type& B)
		{
			return Type(
				FTypeOps<FQuat>::Min(A.GetRotation(), B.GetRotation()),
				A.GetLocation().ComponentMin(B.GetLocation()),
				A.GetScale3D().ComponentMin(B.GetScale3D()));
		}

		static FORCEINLINE Type Max(const Type& A, const Type& B)
		{
			return Type(
				FTypeOps<FQuat>::Max(A.GetRotation(), B.GetRotation()),
				A.GetLocation().ComponentMax(B.GetLocation()),
				A.GetScale3D().ComponentMax(B.GetScale3D()));
		}

		static FORCEINLINE Type Average(const Type& A, const Type& B) { return Lerp(A, B, 0.5); }

		static FORCEINLINE Type WeightedAdd(const Type& A, const Type& B, double W)
		{
			return Type(
				FTypeOps<FQuat>::WeightedAdd(A.GetRotation(), B.GetRotation(), W),
				FTypeOps<FVector>::WeightedAdd(A.GetLocation(), B.GetLocation(), W),
				FTypeOps<FVector>::WeightedAdd(A.GetScale3D(), B.GetScale3D(), W));
		}

		static FORCEINLINE Type WeightedSub(const Type& A, const Type& B, double W)
		{
			return Type(
				FTypeOps<FQuat>::WeightedSub(A.GetRotation(), B.GetRotation(), W),
				FTypeOps<FVector>::WeightedSub(A.GetLocation(), B.GetLocation(), W),
				FTypeOps<FVector>::WeightedSub(A.GetScale3D(), B.GetScale3D(), W));
		}

		static FORCEINLINE Type CopyA(const Type& A, const Type& B) { return A; }
		static FORCEINLINE Type CopyB(const Type& A, const Type& B) { return B; }

		static FORCEINLINE Type UnsignedMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type UnsignedMax(const Type& A, const Type& B) { return Max(A, B); }
		static FORCEINLINE Type AbsoluteMin(const Type& A, const Type& B) { return Min(A, B); }
		static FORCEINLINE Type AbsoluteMax(const Type& A, const Type& B) { return Max(A, B); }

		static FORCEINLINE Type NaiveHash(const Type& A, const Type& B)
		{
			const uint32 H = HashCombine(GetTypeHash(A), GetTypeHash(B));
			return Type(FVector(H, H, H));
		}

		static FORCEINLINE Type UnsignedHash(const Type& A, const Type& B) { return NaiveHash(A, B); }

		static FORCEINLINE Type Abs(const Type& A) { return Type(FTypeOps<FQuat>::Abs(A.GetRotation()), FTypeOps<FVector>::Abs(A.GetLocation()), FTypeOps<FVector>::Abs(A.GetScale3D())); }
		static FORCEINLINE Type Factor(const Type& A, const double Factor) { return Type((A.Rotator() * Factor).Quaternion(), A.GetLocation() * Factor, A.GetScale3D() * Factor); }

		static FORCEINLINE Type ModSimple(const Type& A, double M)
		{
			if (M == 0.0)
			{
				return A;
			}

			FRotator R = A.Rotator();
			R.Pitch = FMath::Fmod(R.Pitch, M);
			R.Yaw = FMath::Fmod(R.Yaw, M);
			R.Roll = FMath::Fmod(R.Roll, M);

			const FVector L = A.GetLocation();
			const FVector S = A.GetScale3D();

			return Type(
				R.Quaternion(),
				FVector(FMath::Fmod(L.X, M), FMath::Fmod(L.Y, M), FMath::Fmod(L.Z, M)),
				FVector(FMath::Fmod(S.X, M), FMath::Fmod(S.Y, M), FMath::Fmod(S.Z, M)));
		}

		static FORCEINLINE Type ModComplex(const Type& A, const Type& B)
		{
			const FRotator RA = A.Rotator();
			const FRotator RB = B.Rotator();
			const FVector LA = A.GetLocation();
			const FVector LB = B.GetLocation();
			const FVector SA = A.GetScale3D();
			const FVector SB = B.GetScale3D();

			FRotator R;
			R.Pitch = RB.Pitch != 0.0 ? FMath::Fmod(RA.Pitch, RB.Pitch) : RA.Pitch;
			R.Yaw = RB.Yaw != 0.0 ? FMath::Fmod(RA.Yaw, RB.Yaw) : RA.Yaw;
			R.Roll = RB.Roll != 0.0 ? FMath::Fmod(RA.Roll, RB.Roll) : RA.Roll;

			return Type(
				R.Quaternion(),
				FVector(
					LB.X != 0.0 ? FMath::Fmod(LA.X, LB.X) : LA.X,
					LB.Y != 0.0 ? FMath::Fmod(LA.Y, LB.Y) : LA.Y,
					LB.Z != 0.0 ? FMath::Fmod(LA.Z, LB.Z) : LA.Z),
				FVector(
					SB.X != 0.0 ? FMath::Fmod(SA.X, SB.X) : SA.X,
					SB.Y != 0.0 ? FMath::Fmod(SA.Y, SB.Y) : SA.Y,
					SB.Z != 0.0 ? FMath::Fmod(SA.Z, SB.Z) : SA.Z));
		}

		static FORCEINLINE Type Weight(const Type& A, const Type& B, double W)
		{
			return W != 0.0 ? Div(Add(A, B), W) : A;
		}

		static FORCEINLINE Type NormalizeWeight(const Type& A, double TW)
		{
			const double InvWeight = TW != 0.0 ? 1.0 / TW : 1;
			return Type(
				A.GetRotation().GetNormalized(),
				A.GetLocation() * InvWeight,
				A.GetScale3D() * InvWeight);
		}

		static FORCEINLINE double ExtractField(const void* Value, ESingleField Field)
		{
			const FVector Pos = static_cast<const FTransform*>(Value)->GetLocation();
			return FTypeOps<FVector>::ExtractField(&Pos, Field);
		}

		static FORCEINLINE void InjectField(void* Target, double Value, ESingleField Field)
		{
			Type& T = *static_cast<Type*>(Target);
			FVector Pos = T.GetLocation();
			FTypeOps<FVector>::InjectField(&T, Value, Field);
			T.SetLocation(Pos);
		}

		static FORCEINLINE FVector ExtractAxis(const void* Value, EPCGExAxis Axis)
		{
			return PCGExMath::GetDirection(static_cast<const Type*>(Value)->GetRotation(), Axis);
		}

		static FORCEINLINE void ExtractComponent(const void* Transform, ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType)
		{
			const Type& T = *static_cast<const Type*>(Transform);
			switch (Part)
			{
			case ETransformPart::Position: *static_cast<FVector*>(OutValue) = T.GetLocation();
				OutType = EPCGMetadataTypes::Vector;
				break;
			case ETransformPart::Rotation: *static_cast<FQuat*>(OutValue) = T.GetRotation();
				OutType = EPCGMetadataTypes::Quaternion;
				break;
			case ETransformPart::Scale: *static_cast<FVector*>(OutValue) = T.GetScale3D();
				OutType = EPCGMetadataTypes::Vector;
				break;
			}
		}

		static FORCEINLINE void ExtractVector(const void* Transform, ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType)
		{
			const Type& T = *static_cast<const Type*>(Transform);
			switch (Part)
			{
			case ETransformPart::Position: *static_cast<FVector*>(OutValue) = T.GetLocation();
				OutType = EPCGMetadataTypes::Vector;
				break;
			case ETransformPart::Scale: *static_cast<FVector*>(OutValue) = T.GetScale3D();
				OutType = EPCGMetadataTypes::Vector;
				break;
			}
		}

		static FORCEINLINE void ExtractQuat(const void* Transform, ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType)
		{
			const Type& T = *static_cast<const Type*>(Transform);
			*static_cast<FQuat*>(OutValue) = T.GetRotation();
			OutType = EPCGMetadataTypes::Quaternion;
		}

		static FORCEINLINE void InjectComponent(void* Transform, ETransformPart Part, const void* Value, EPCGMetadataTypes ValueType)
		{
			Type& T = *static_cast<Type*>(Transform);
			switch (Part)
			{
			case ETransformPart::Position: T.SetLocation(*static_cast<const FVector*>(Value));
				break;
			case ETransformPart::Rotation: T.SetRotation(*static_cast<const FQuat*>(Value));
				break;
			case ETransformPart::Scale: T.SetScale3D(*static_cast<const FVector*>(Value));
				break;
			}
		}
	};
}
