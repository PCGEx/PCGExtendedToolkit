// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

// Core type operations
#include "Types/PCGExTypeOps.h"

// Type-specific implementations
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Types/PCGExTypeOpsNumeric.h"
#include "Types/PCGExTypeOpsVector.h"
#include "Types/PCGExTypeOpsRotation.h"
#include "Types/PCGExTypeOpsString.h"

// Type-erased buffers
//#include "PCGExTypeErasedBuffer.h"

class FPCGMetadataAttributeBase;

namespace PCGExTypes
{
	//
	// FScopedTypedValue - RAII wrapper for type-erased values
	//
	// Provides safe lifecycle management for both POD and complex types (FString, FName, etc.).
	// Uses an inline stack buffer for known types (up to 96 bytes), and heap-allocates for
	// generic/unknown types whose size is only known at runtime.
	//
	class PCGEXCORE_API FScopedTypedValue
	{
	public:
		// Calculate maximum size needed across all supported types
		// FTransform is the largest at 80 bytes (FQuat 32 + FVector 24 + FVector 24)
		// Add padding for safety and alignment
		static constexpr int32 BufferSize = 96;
		static constexpr int32 BufferAlignment = 16;

		// Static assertions to ensure buffer is large enough for all types
#define PCGEX_TPL(_TYPE, _NAME, ...) static_assert(BufferSize >= sizeof(_TYPE), "Buffer too small for "#_NAME);
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	private:
		alignas(BufferAlignment) uint8 Storage[BufferSize];
		void* ActiveStorage = Storage; // Points to Storage (inline) or heap allocation
		EPCGMetadataTypes Type;
		int32 ValueSize = 0; // Actual size of stored value (0 = computed from Type)
		bool bConstructed;
		bool bHeapAllocated = false;

	public:
		// Construct with known type - uses inline stack buffer
		explicit FScopedTypedValue(EPCGMetadataTypes InType);

		// Construct with explicit size - heap-allocates if size > BufferSize
		FScopedTypedValue(EPCGMetadataTypes InType, int32 InSize, int32 InAlignment = 1);

		// Destructor - calls destructor for complex types, frees heap if allocated
		~FScopedTypedValue();

		// Non-copyable to prevent double-destruction
		FScopedTypedValue(const FScopedTypedValue&) = delete;
		FScopedTypedValue& operator=(const FScopedTypedValue&) = delete;

		// Move constructor
		FScopedTypedValue(FScopedTypedValue&& Other) noexcept;
		FScopedTypedValue& operator=(FScopedTypedValue&&) = delete;

		// Raw access - returns active storage pointer (inline or heap)
		FORCEINLINE void* GetRaw() { return ActiveStorage; }
		FORCEINLINE const void* GetRaw() const { return ActiveStorage; }

		// Typed access
		template <typename T>
		FORCEINLINE T& As() { return *reinterpret_cast<T*>(ActiveStorage); }

		template <typename T>
		FORCEINLINE const T& As() const { return *reinterpret_cast<const T*>(ActiveStorage); }

		// Type info
		FORCEINLINE EPCGMetadataTypes GetType() const { return Type; }
		FORCEINLINE bool IsConstructed() const { return bConstructed; }
		FORCEINLINE int32 GetValueSize() const { return ValueSize; }
		FORCEINLINE bool IsHeapAllocated() const { return bHeapAllocated; }

		// Manual lifecycle control (for reuse scenarios)
		void Destruct();
		void Initialize(EPCGMetadataTypes NewType);
		void Initialize(EPCGMetadataTypes NewType, int32 InSize, int32 InAlignment = 1);

		// Type traits helpers
		static bool NeedsLifecycleManagement(EPCGMetadataTypes InType);
		static int32 GetTypeSize(EPCGMetadataTypes InType);

	private:
		void AllocateStorage(int32 InSize, int32 InAlignment);
		void FreeHeapStorage();
		void InitializeValue();
	};


	// Get element size for generic/unknown attribute types from their descriptor.
	// Returns 0 if the size cannot be determined (e.g., container types).
	PCGEXCORE_API int32 GetElementSizeFromType(EPCGMetadataTypes InType, const UObject* ValueTypeObject = nullptr);

	// Get element alignment for generic/unknown attribute types.
	PCGEXCORE_API int32 GetElementAlignmentFromType(EPCGMetadataTypes InType, const UObject* ValueTypeObject = nullptr);

	// Get element size from an attribute base pointer (handles both typed and generic).
	PCGEXCORE_API int32 GetElementSizeFromAttribute(const FPCGMetadataAttributeBase* InAttribute);

	/**
	 * Convenience functions for common operations
	 */

	// Convert between types (compile-time)
	template <typename TFrom, typename TTo>
	FORCEINLINE TTo Convert(const TFrom& Value)
	{
		return PCGExTypeOps::FTypeOps<TFrom>::template ConvertTo<TTo>(Value);
	}

	// Compute hash for any supported type
	template <typename T>
	FORCEINLINE uint32 ComputeHash(const T& Value)
	{
		return PCGExTypeOps::FTypeOps<T>::Hash(Value);
	}

	// Check if two values are equal
	template <typename T>
	FORCEINLINE bool AreEqual(const T& A, const T& B)
	{
		return A == B;
	}

	// Lerp between values
	template <typename T>
	FORCEINLINE T Lerp(const T& A, const T& B, double Alpha)
	{
		return PCGExTypeOps::FTypeOps<T>::Lerp(A, B, Alpha);
	}

	// Clamp to min/max
	template <typename T>
	FORCEINLINE T Clamp(const T& Value, const T& MinVal, const T& MaxVal)
	{
		T Result = PCGExTypeOps::FTypeOps<T>::Max(Value, MinVal);
		return PCGExTypeOps::FTypeOps<T>::Min(Result, MaxVal);
	}

	template <typename T>
	FORCEINLINE T Abs(const T& A)
	{
		return PCGExTypeOps::FTypeOps<T>::Abs(A);
	}

	template <typename T>
	FORCEINLINE T Factor(const T& A, const double Factor)
	{
		return PCGExTypeOps::FTypeOps<T>::Factor(A, Factor);
	}
}
