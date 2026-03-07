// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSubSelection.h"
#include "Types/PCGExTypeOps.h"

/**
 * PCGEx Sub-Selection Operations System
 * 
 * This system provides type-erased sub-selection operations (field extraction,
 * component selection, axis direction) separate from type conversion.
 * 
 * Architecture:
 * - ISubSelectorOps: Type-erased interface for sub-selection operations per type
 * - TSubSelectorOpsImpl<T>: Per-type implementation (14 instantiations total)
 * - FSubSelectorRegistry: Runtime lookup by EPCGMetadataTypes
 * 
 * The sub-selection system handles:
 * - Field extraction (X, Y, Z, W, Length, Volume, etc.) → double
 * - Field injection (double → X, Y, Z, W)
 * - Axis direction extraction (FQuat/FRotator → FVector)
 * - Transform component extraction/injection
 * 
 * Type conversion between different types is delegated to FConversionTable.
 */

namespace PCGExData
{
	// Forward declarations
	class ISubSelectorOps;

	/**
	 * Function pointer types for sub-selection operations
	 */

	// Extract field to double: double = ExtractField(const void* Value, int32 FieldIndex)
	using FExtractFieldFn = double (*)(const void* Value, PCGExTypeOps::ESingleField Field);

	// Inject field from double: void InjectField(void* Target, double Value, int32 FieldIndex)  
	using FInjectFieldFn = void (*)(void* Target, double Value, PCGExTypeOps::ESingleField Field);

	// Extract axis direction: FVector = ExtractAxis(const void* RotationValue, EPCGExAxis Axis)
	using FExtractAxisFn = FVector (*)(const void* Value, EPCGExAxis Axis);

	/**
	 * ISubSelectorOps - Type-erased interface for sub-selection operations
	 * 
	 * Each supported type gets one implementation. Operations are specific to
	 * what makes sense for that type:
	 * - Numeric types: Only support identity (field 0)
	 * - Vector types: Support X, Y, Z (W for Vector4), Length, Volume, Sum
	 * - Rotation types: Support axis extraction, field extraction via Rotator
	 * - Transform: Support component extraction (Position, Rotation, Scale)
	 * - String types: No sub-selection (identity only)
	 */
	class PCGEXCORE_API ISubSelectorOps
	{
	public:
		virtual ~ISubSelectorOps() = default;

		// Type information
		virtual EPCGMetadataTypes GetTypeId() const = 0;
		virtual int32 GetNumFields() const = 0;
		virtual bool SupportsFieldExtraction() const = 0;
		virtual bool SupportsAxisExtraction() const = 0;
		virtual bool SupportsComponentExtraction() const = 0;

		/**
		 * Extract a scalar field value from this type
		 * 
		 * @param Value Pointer to source value of this type
		 * @param Field Which field to extract (X, Y, Z, W, Length, etc.)
		 * @return Extracted scalar as double
		 */
		virtual double ExtractField(const void* Value, PCGExTypeOps::ESingleField Field) const = 0;

		/**
		 * Inject a scalar value into a specific field
		 * 
		 * @param Target Pointer to target value of this type (modified in place)
		 * @param Value Scalar value to inject
		 * @param Field Which field to set
		 */
		virtual void InjectField(void* Target, double Value, PCGExTypeOps::ESingleField Field) const = 0;

		/**
		 * Extract an axis direction vector (for rotation types only)
		 * 
		 * @param Value Pointer to rotation value (FQuat or FRotator)
		 * @param Axis Which axis direction to extract
		 * @return Direction vector
		 */
		virtual FVector ExtractAxis(const void* Value, EPCGExAxis Axis) const = 0;

		/**
		 * Extract a transform component (for FTransform only)
		 * 
		 * @param Transform Pointer to FTransform
		 * @param Part Which component (Position, Rotation, Scale)
		 * @param OutValue Pointer to output (FVector for Position/Scale, FQuat for Rotation)
		 * @param OutType Receives the type of the extracted component
		 */
		virtual void ExtractComponent(const void* Transform, PCGExTypeOps::ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType) const = 0;

		/**
		 * Inject a component into a transform (for FTransform only)
		 * 
		 * @param Transform Pointer to FTransform (modified in place)
		 * @param Part Which component to set
		 * @param Value Pointer to input value (FVector for Position/Scale, FQuat for Rotation)
		 * @param ValueType Type of the input value
		 */
		virtual void InjectComponent(void* Transform, PCGExTypeOps::ETransformPart Part, const void* Value, EPCGMetadataTypes ValueType) const = 0;

		/**
		 * Convert this type to a "working" type suitable for the sub-selection
		 * 
		 * For field extraction → double
		 * For axis extraction → FVector  
		 * For component extraction → FVector or FQuat depending on component
		 * 
		 * @param Value Pointer to source value
		 * @param Selection The sub-selection configuration
		 * @param OutValue Pointer to output buffer (must be large enough)
		 * @param OutType Receives the output type
		 */
		virtual void ApplyGetSelection(const void* Value, const FSubSelection& Selection, void* OutValue, EPCGMetadataTypes& OutType) const = 0;

		/**
		 * Apply a sub-selection when setting a value
		 * 
		 * @param Target Pointer to target value (modified in place)
		 * @param Selection The sub-selection configuration
		 * @param Source Pointer to source value
		 * @param SourceType Type of the source value
		 */
		virtual void ApplySetSelection(void* Target, const FSubSelection& Selection, const void* Source, EPCGMetadataTypes SourceType) const = 0;
	};

	/**
	 * FSubSelectorRegistry - Global registry for sub-selector operations
	 * 
	 * Provides O(1) lookup of sub-selector ops by EPCGMetadataTypes.
	 */
	class PCGEXCORE_API FSubSelectorRegistry
	{
	public:
		// Get ops for a specific type
		static const ISubSelectorOps* Get(EPCGMetadataTypes Type);

		// Get ops with template type
		template <typename T>
		static const ISubSelectorOps* Get();

		// Initialize the registry (called automatically on first access)
		static void Initialize();

	private:
		static TArray<TUniquePtr<ISubSelectorOps>> Ops;
		static bool bInitialized;
	};

	/**
	 * Type traits for sub-selection capabilities
	 */
	template <typename T>
	struct TSubSelectionTraits
	{
		static constexpr bool bSupportsFieldExtraction = false;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	// Numeric types - single field only
	template <>
	struct TSubSelectionTraits<bool>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<int32>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<int64>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<float>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<double>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	// Vector types - multiple fields
	template <>
	struct TSubSelectionTraits<FVector2D>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 2;
	};

	template <>
	struct TSubSelectionTraits<FVector>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 3;
	};

	template <>
	struct TSubSelectionTraits<FVector4>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 4;
	};

	// Rotation types - fields + axis extraction
	template <>
	struct TSubSelectionTraits<FQuat>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = true;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 3; // Roll, Pitch, Yaw (via Rotator)
	};

	template <>
	struct TSubSelectionTraits<FRotator>
	{
		static constexpr bool bSupportsFieldExtraction = true;
		static constexpr bool bSupportsAxisExtraction = true;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 3;
	};

	// Transform - component extraction
	template <>
	struct TSubSelectionTraits<FTransform>
	{
		static constexpr bool bSupportsFieldExtraction = true; // Via components
		static constexpr bool bSupportsAxisExtraction = true;  // Via rotation component
		static constexpr bool bSupportsComponentExtraction = true;
		static constexpr int32 NumFields = 3; // 3 pos + 3 rot + 3 scale
	};

	// String types - no sub-selection
	template <>
	struct TSubSelectionTraits<FString>
	{
		static constexpr bool bSupportsFieldExtraction = false;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<FName>
	{
		static constexpr bool bSupportsFieldExtraction = false;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<FSoftObjectPath>
	{
		static constexpr bool bSupportsFieldExtraction = false;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};

	template <>
	struct TSubSelectionTraits<FSoftClassPath>
	{
		static constexpr bool bSupportsFieldExtraction = false;
		static constexpr bool bSupportsAxisExtraction = false;
		static constexpr bool bSupportsComponentExtraction = false;
		static constexpr int32 NumFields = 1;
	};
}
