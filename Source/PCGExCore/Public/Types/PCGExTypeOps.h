// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExTypeTraits.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "UObject/SoftObjectPath.h"

/**
 * PCGEx Type Operations System
 * 
 * This system centralizes all type-specific operations (conversion, blending, hashing)
 * into per-type semantic objects. Instead of 14×14×22 template instantiations,
 * we have 14 type operation objects + runtime dispatch.
 * 
 * Architecture:
 * - FTypeOps<T>: Static per-type operations (conversion, blend functions, hash)
 * - ITypeOpsBase: Type-erased interface for runtime dispatch
 * - FTypeOpsRegistry: Global registry mapping EPCGMetadataTypes to ops
 */

namespace PCGExTypeOps
{
	/**
	 * Single field selection identifiers
	 */
	enum class PCGEXCORE_API ESingleField : uint8
	{
		X             = 0,
		Y             = 1,
		Z             = 2,
		W             = 3,
		Length        = 4,
		SquaredLength = 5,
		Volume        = 6,
		Sum           = 7,
	};

	/**
	 * Transform component parts
	 */
	enum class PCGEXCORE_API ETransformPart : uint8
	{
		Position = 0,
		Rotation = 1,
		Scale    = 2,
	};

	// Forward declarations
	class ITypeOpsBase;

	template <typename T>
	struct FTypeOps;

	/**
	 * TIsCategoricalBlend<T> -- true for "string-like" types (FName, FString, soft paths) that have
	 * no additive or averaging structure. Accumulating blend modes (Average, Mult, WeightedAdd, ...)
	 * must select a representative value for these rather than combine them: concatenation grows
	 * unbounded and trips FName's hard 1023-character assert once enough values are folded together.
	 *
	 * Deliberately NARROWER than TTraits<T>::bSupportsArithmetic, which is also false for Quat/Transform/bool
	 * -- those have genuine (slerp/composition) blend math that must NOT degrade to selection.
	 */
	template <typename T>
	struct TIsCategoricalBlend
	{
		static constexpr bool Value = false;
	};

	template <> struct TIsCategoricalBlend<FName> { static constexpr bool Value = true; };
	template <> struct TIsCategoricalBlend<FString> { static constexpr bool Value = true; };
	template <> struct TIsCategoricalBlend<FSoftObjectPath> { static constexpr bool Value = true; };
	template <> struct TIsCategoricalBlend<FSoftClassPath> { static constexpr bool Value = true; };

	// Type-Erased Operation Interface

	/**
	 * Type-erased interface for all type operations.
	 * Allows runtime dispatch without templates at call sites.
	 */
	class PCGEXCORE_API ITypeOpsBase
	{
	public:
		virtual ~ITypeOpsBase() = default;

		// Type information
		virtual EPCGMetadataTypes GetTypeId() const = 0;
		virtual FString GetTypeName() const = 0;
		virtual int32 GetTypeSize() const = 0;
		virtual int32 GetTypeAlignment() const = 0;
		virtual bool SupportsLerp() const = 0;
		virtual bool SupportsMinMax() const = 0;
		virtual bool SupportsArithmetic() const = 0;

		virtual bool SameType(const ITypeOpsBase* Other) const
		{
			return GetTypeId() == Other->GetTypeId();
		}

		// Default value operations
		virtual void SetDefault(void* OutValue) const = 0;
		virtual void Copy(const void* Src, void* Dst) const = 0;

		// Hash operations
		virtual PCGExValueHash ComputeHash(const void* Value) const = 0;

		// Conversion - implemented by derived to avoid virtual call overhead
		// Convert from this type to target type
		virtual void ConvertTo(const void* SrcValue, EPCGMetadataTypes TargetType, void* OutValue) const = 0;
		virtual void ConvertFrom(EPCGMetadataTypes SrcType, const void* SrcValue, void* OutValue) const = 0;

		// Blend operations - type-erased versions
		// All blend functions: void Blend(const void* A, const void* B, void* Out, double Weight)
		virtual void BlendAdd(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendSub(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendMult(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendDiv(const void* A, double Divisor, void* Out) const = 0;
		virtual void BlendLerp(const void* A, const void* B, double Weight, void* Out) const = 0;
		virtual void BlendMin(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendMax(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendAverage(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendWeightedAdd(const void* A, const void* B, double Weight, void* Out) const = 0;
		virtual void BlendWeightedSub(const void* A, const void* B, double Weight, void* Out) const = 0;
		virtual void BlendCopyA(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendCopyB(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendUnsignedMin(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendUnsignedMax(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendAbsoluteMin(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendAbsoluteMax(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendHash(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendUnsignedHash(const void* A, const void* B, void* Out) const = 0;
		virtual void BlendModSimple(const void* A, double Modulo, void* Out) const = 0;
		virtual void BlendModComplex(const void* A, const void* B, void* Out) const = 0;


		// Weight/Average accumulation helpers
		virtual void BlendWeight(const void* A, const void* B, double Weight, void* Out) const = 0;
		virtual void NormalizeWeight(const void* A, double TotalWeight, void* Out) const = 0;

		virtual void Abs(const void* A, void* Out) const = 0;
		virtual void Factor(const void* A, const double Factor, void* Out) const = 0;
	};

	// Type Operations Registry

	/**
	 * Global registry for type operations.
	 */
	class PCGEXCORE_API FTypeOpsRegistry
	{
	public:
		static constexpr int32 NumSupportedTypes = 14;

		static const ITypeOpsBase* Get(EPCGMetadataTypes Type);

		// Get ops with template type
		template <typename T>
		static const ITypeOpsBase* Get();

		// Type ID conversion
		template <typename T>
		static EPCGMetadataTypes GetTypeId();

		// Initialize the registry (called automatically)
		static void Initialize();

	private:
		static TArray<TUniquePtr<ITypeOpsBase>> TypeOps;
		static bool bInitialized;
	};

	// Conversion Function Pointer Types

	// Function pointer type for conversion: void Convert(const void* Src, void* Dst)
	using FConvertFn = void(*)(const void* Src, void* Dst);

	/**
	 * Conversion dispatch table.
	 * 14x14 table of function pointers for all type pair conversions.
	 */
	class PCGEXCORE_API FConversionTable
	{
	public:
		// Convert between any two supported types
		FORCEINLINE static void Convert(
			EPCGMetadataTypes FromType, const void* FromValue,
			EPCGMetadataTypes ToType, void* ToValue)
		{
			if (!bInitialized)
			{
				Initialize();
			}
			Table[static_cast<int32>(FromType)][static_cast<int32>(ToType)](FromValue, ToValue);
		}

		// Get the conversion function pointer for a specific pair
		FORCEINLINE static FConvertFn GetConversionFn(EPCGMetadataTypes FromType, EPCGMetadataTypes ToType)
		{
			if (!bInitialized)
			{
				Initialize();
			}
			return Table[static_cast<int32>(FromType)][static_cast<int32>(ToType)];
		}

		// Initialize the table (called automatically)
		static void Initialize();

	private:
		static FConvertFn Table[PCGExTypes::TypesAllocations][PCGExTypes::TypesAllocations];
		static bool bInitialized;
	};

	// Blend Function Pointer Types

	// Standard blend: C = Blend(A, B)
	using FBlendBinaryFn = void(*)(const void* A, const void* B, void* Out);
	// Weighted blend: C = Blend(A, B, Weight)
	using FBlendWeightedFn = void(*)(const void* A, const void* B, double Weight, void* Out);
	// Scalar blend: C = Blend(A, Scalar)
	using FBlendScalarFn = void(*)(const void* A, double Scalar, void* Out);

	// Type-Safe Wrapper Templates

	/**
	 * Type-safe conversion wrapper that uses the type-erased system.
	 */
	template <typename TFrom, typename TTo>
	FORCEINLINE TTo Convert(const TFrom& Value)
	{
		TTo Result{};
		FConversionTable::Convert(PCGExTypes::TTraits<TFrom>::Type, &Value, PCGExTypes::TTraits<TTo>::Type, &Result);
		return Result;
	}

	/**
	 * Type-safe hash wrapper.
	 */
	template <typename T>
	FORCEINLINE PCGExValueHash ComputeHash(const T& Value)
	{
		const ITypeOpsBase* Ops = FTypeOpsRegistry::Get<T>();
		return Ops->ComputeHash(&Value);
	}
}
