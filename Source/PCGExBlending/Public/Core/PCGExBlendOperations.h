// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "PCGExOpStats.h"
#include "Types/PCGExTypeOpsImpl.h"

namespace PCGEx
{
	struct FOpStats;
}

namespace PCGExBlending
{
	class IBlendOperation;

	//
	// Blend Function Pointer Types
	//
	// Standard blend: Out = Blend(A, B, Weight)
	using FBlendFn = void (*)(const void* A, const void* B, double Weight, void* Out);

	// Finalize: Acc = Finalize(Acc, TotalWeight, Count)
	using FFinalizeFn = void (*)(void* Accumulator, double TotalWeight, int32 Count);

	//
	// IBlendOperation - Type-erased interface for blend operations
	//
	// Provides a runtime-polymorphic interface for blending values of any type.
	// Eliminates the need for template instantiation per blend mode.
	//
	class PCGEXBLENDING_API IBlendOperation
	{
	protected:
		EPCGExABBlendingType Mode;
		const bool bResetForMulti;
		const bool bInitWithSource;
		const bool bConsiderOriginalValue;

		FBlendFn BlendFunc = nullptr;
		FBlendFn AccumulateFunc = nullptr;
		FFinalizeFn FinalizeFunc = nullptr;

	public:
		IBlendOperation(EPCGExABBlendingType InMode, bool bInResetForMulti);
		virtual ~IBlendOperation() = default;

		// Core blend: Out = Blend(A, B, Weight)
		// Virtual to allow FCopyOnlyBlendOperation to override with size-aware memcpy.
		// Default implementation dispatches through function pointer (zero overhead for typed ops).
		virtual void Blend(const void* A, const void* B, double Weight, void* Out) const { BlendFunc(A, B, Weight, Out); }

		// Multi-blend operations for accumulation patterns
		virtual void BeginMulti(void* Accumulator, const void* InitialValue, PCGEx::FOpStats& OutTracker) const
		{
			// These modes require the first operation to be a copy of the first blended value
			// before they can be properly blended -- this should be handled by the blend op?
			if (bInitWithSource) { OutTracker.Count = -1; }
			else if (bConsiderOriginalValue)
			{
				// Some BlendModes can leverage this
				if (bResetForMulti) { InitDefault(Accumulator); }
				else
				{
					// Otherwise, bump up original count so EndBlend can account for pre-existing value as "one blend step"
					OutTracker.Count = 1;
					OutTracker.TotalWeight = 1;
				}
			}
		}

		virtual void Accumulate(const void* Source, void* Accumulator, double Weight) const { AccumulateFunc(Accumulator, Source, Weight, Accumulator); }
		virtual void EndMulti(void* Accumulator, double TotalWeight, int32 Count) const { FinalizeFunc(Accumulator, TotalWeight, Count); }

		// Division helper (for external averaging)
		virtual void Div(void* Value, double Divisor) const = 0;

		// Properties
		virtual EPCGMetadataTypes GetWorkingType() const = 0;
		FORCEINLINE EPCGExABBlendingType GetBlendMode() const { return Mode; }
		FORCEINLINE bool RequiresReset() const { return bResetForMulti; }

		// Stack buffer helpers
		virtual int32 GetValueSize() const = 0;
		virtual int32 GetValueAlignment() const = 0;

		// Default value initialization
		virtual void InitDefault(void* Value) const = 0;

		// Lifecycle management for complex types
		virtual bool NeedsLifecycleManagement() const = 0;
		virtual void ConstructValue(void* Value) const = 0;
		virtual void DestroyValue(void* Value) const = 0;
		virtual void CopyValue(const void* Src, void* Dst) const = 0;
	};

	//
	// BlendFunctions namespace - Static blend function implementations per type
	//
	// Centralizes all blend mode implementations. Each function is shared
	// across all uses of that blend mode for a given type.
	//
	namespace BlendFunctions
	{
		// Generic implementations using FTypeOps

		template <typename T>
		void Add(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Add(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Sub(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Sub(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Mult(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Mult(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Divide(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			const double Divisor = PCGExTypeOps::FTypeOps<T>::template ConvertTo<double>(*static_cast<const T*>(B));
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Div(*static_cast<const T*>(A), Divisor);
		}

		template <typename T>
		void Lerp(const void* A, const void* B, double Weight, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Lerp(*static_cast<const T*>(A), *static_cast<const T*>(B), Weight);
		}

		template <typename T>
		void Min(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Min(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Max(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Max(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Average(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::Average(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void Weight(const void* A, const void* B, double Weight, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::WeightedAdd(*static_cast<const T*>(A), *static_cast<const T*>(B), Weight);
		}

		template <typename T>
		void WeightedAdd(const void* A, const void* B, double Weight, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::WeightedAdd(*static_cast<const T*>(A), *static_cast<const T*>(B), Weight);
		}

		template <typename T>
		void WeightedSub(const void* A, const void* B, double Weight, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::WeightedSub(*static_cast<const T*>(A), *static_cast<const T*>(B), Weight);
		}

		template <typename T>
		void CopyA(const void* A, const void* /*B*/, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = *static_cast<const T*>(A);
		}

		template <typename T>
		void CopyB(const void* /*A*/, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = *static_cast<const T*>(B);
		}

		template <typename T>
		void UnsignedMin(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::UnsignedMin(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void UnsignedMax(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::UnsignedMax(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void AbsoluteMin(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::AbsoluteMin(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void AbsoluteMax(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::AbsoluteMax(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void NaiveHash(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::NaiveHash(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void UnsignedHash(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::UnsignedHash(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void ModSimple(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			const double Modulo = PCGExTypeOps::FTypeOps<T>::template ConvertTo<double>(*static_cast<const T*>(B));
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::ModSimple(*static_cast<const T*>(A), Modulo);
		}

		template <typename T>
		void ModComplex(const void* A, const void* B, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = PCGExTypeOps::FTypeOps<T>::ModComplex(*static_cast<const T*>(A), *static_cast<const T*>(B));
		}

		template <typename T>
		void None(const void* A, const void* /*B*/, double /*Weight*/, void* Out)
		{
			*static_cast<T*>(Out) = *static_cast<const T*>(A);
		}

		// Get blend function pointer by mode
		template <typename T>
		FBlendFn GetBlendFunction(const EPCGExABBlendingType Mode)
		{
			switch (Mode)
			{
			case EPCGExABBlendingType::Add: return &Add<T>;
			case EPCGExABBlendingType::Subtract: return &Sub<T>;
			case EPCGExABBlendingType::Multiply: return &Mult<T>;
			case EPCGExABBlendingType::Divide: return &Divide<T>;
			case EPCGExABBlendingType::Lerp: return &Lerp<T>;
			case EPCGExABBlendingType::Min: return &Min<T>;
			case EPCGExABBlendingType::Max: return &Max<T>;
			case EPCGExABBlendingType::Average: return &Average<T>;
			case EPCGExABBlendingType::Weight: return &Weight<T>;
			case EPCGExABBlendingType::WeightedAdd: return &WeightedAdd<T>;
			case EPCGExABBlendingType::WeightedSubtract: return &WeightedSub<T>;
			case EPCGExABBlendingType::CopyTarget: return &CopyA<T>;
			case EPCGExABBlendingType::CopySource: return &CopyB<T>;
			case EPCGExABBlendingType::UnsignedMin: return &UnsignedMin<T>;
			case EPCGExABBlendingType::UnsignedMax: return &UnsignedMax<T>;
			case EPCGExABBlendingType::AbsoluteMin: return &AbsoluteMin<T>;
			case EPCGExABBlendingType::AbsoluteMax: return &AbsoluteMax<T>;
			case EPCGExABBlendingType::Hash: return &NaiveHash<T>;
			case EPCGExABBlendingType::UnsignedHash: return &UnsignedHash<T>;
			case EPCGExABBlendingType::Mod: return &ModSimple<T>;
			case EPCGExABBlendingType::ModCW: return &ModComplex<T>;
			case EPCGExABBlendingType::WeightNormalize: return &Weight<T>; // TBD
			case EPCGExABBlendingType::GeometricMean: return &Weight<T>;   // TBD
			case EPCGExABBlendingType::HarmonicMean: return &Weight<T>;    // TBD
			case EPCGExABBlendingType::RMS: return &Weight<T>;             // TBD
			case EPCGExABBlendingType::Step: return &Weight<T>;            // TBD
			case EPCGExABBlendingType::None:
			default: return &None<T>;
			}
		}

		// Get accumulate blend function pointer by mode
		template <typename T>
		FBlendFn GetAccumulateFunction(const EPCGExABBlendingType Mode)
		{
			switch (Mode)
			{
			case EPCGExABBlendingType::Average: return &Add<T>; // Average does /2 internally, we don't want to accumulate that
			default: return GetBlendFunction<T>(Mode);
			}
		}

		// Finalize functions for multi-blend

		template <typename T>
		void FinalizeAverage(void* Accumulator, double /*TotalWeight*/, int32 Count)
		{
			if (Count > 0)
			{
				T& Acc = *static_cast<T*>(Accumulator);
				Acc = PCGExTypeOps::FTypeOps<T>::Div(Acc, static_cast<double>(Count));
			}
		}

		template <typename T>
		void FinalizeWeight(void* Accumulator, double TotalWeight, int32 /*Count*/)
		{
			if (TotalWeight > 1.0)
			{
				T& Acc = *static_cast<T*>(Accumulator);
				Acc = PCGExTypeOps::FTypeOps<T>::NormalizeWeight(Acc, TotalWeight);
			}
		}

		template <typename T>
		void FinalizeWeightNormalize(void* Accumulator, double TotalWeight, int32 /*Count*/)
		{
			T& Acc = *static_cast<T*>(Accumulator);
			Acc = PCGExTypeOps::FTypeOps<T>::NormalizeWeight(Acc, FMath::Max(TotalWeight, 1));
		}

		template <typename T>
		void FinalizeNoop(void* /*Accumulator*/, double /*TotalWeight*/, int32 /*Count*/)
		{
			// No finalization needed
		}

		// Get finalize function by mode
		template <typename T>
		FFinalizeFn GetFinalizeFunction(const EPCGExABBlendingType Mode)
		{
			switch (Mode)
			{
			case EPCGExABBlendingType::Average: return &FinalizeAverage<T>;
			case EPCGExABBlendingType::Weight: return &FinalizeWeight<T>;
			case EPCGExABBlendingType::WeightNormalize: return &FinalizeWeightNormalize<T>;
			default: return &FinalizeNoop<T>;
			}
		}

		// Division helper

		template <typename T>
		void DivValue(void* Value, double Divisor)
		{
			if (Divisor != 0.0)
			{
				T& Val = *static_cast<T*>(Value);
				Val = PCGExTypeOps::FTypeOps<T>::Div(Val, Divisor);
			}
		}
	}

	//
	// TBlendOperationImpl<T> - Templated implementation of IBlendOperation
	//
	// Only 14 instantiations (one per type) instead of 616.
	// Blend mode selection is done via function pointer at construction time.
	//
	template <typename T>
	class TBlendOperationImpl final : public IBlendOperation
	{
	public:
		TBlendOperationImpl(EPCGExABBlendingType InMode, bool bInResetForMulti)
			: IBlendOperation(InMode, bInResetForMulti)
		{
			BlendFunc = BlendFunctions::GetBlendFunction<T>(InMode);
			AccumulateFunc = BlendFunctions::GetAccumulateFunction<T>(InMode);
			FinalizeFunc = BlendFunctions::GetFinalizeFunction<T>(InMode);
		}

		//~ Begin IBlendOperation interface

		virtual void Div(void* Value, double Divisor) const override
		{
			BlendFunctions::DivValue<T>(Value, Divisor);
		}

		virtual EPCGMetadataTypes GetWorkingType() const override { return PCGExTypes::TTraits<T>::Type; }
		virtual int32 GetValueSize() const override { return sizeof(T); }
		virtual int32 GetValueAlignment() const override { return alignof(T); }
		virtual void InitDefault(void* Value) const override { new(Value) T(); }
		virtual bool NeedsLifecycleManagement() const override { return !std::is_trivially_copyable_v<T>; }
		virtual void ConstructValue(void* Value) const override { new(Value) T(); }
		virtual void DestroyValue(void* Value) const override { static_cast<T*>(Value)->~T(); }
		virtual void CopyValue(const void* Src, void* Dst) const override { *static_cast<T*>(Dst) = *static_cast<const T*>(Src); }

		//~ End IBlendOperation interface
	};

	// Extern template declarations (instantiated in cpp)
#define PCGEX_DECLARE_BLEND_OP_EXTERN(_TYPE, _NAME, ...) \
	extern template class TBlendOperationImpl<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_DECLARE_BLEND_OP_EXTERN)
#undef PCGEX_DECLARE_BLEND_OP_EXTERN

	//
	// FCopyOnlyBlendOperation - Fallback for unknown/generic attribute types.
	//
	// Carries the value size so it can memcpy for CopyA/CopyB/None semantics.
	// All arithmetic modes (Add, Lerp, etc.) fall back to CopyA to preserve data.
	//
	class PCGEXBLENDING_API FCopyOnlyBlendOperation final : public IBlendOperation
	{
		int32 ValueSize;
		int32 ValueAlignment;

	public:
		FCopyOnlyBlendOperation(int32 InValueSize, int32 InValueAlignment, EPCGExABBlendingType InMode, bool bInResetForMulti)
			: IBlendOperation(InMode, bInResetForMulti)
			, ValueSize(InValueSize)
			, ValueAlignment(InValueAlignment)
		{
			// Function pointers unused — all dispatch goes through virtual overrides
			BlendFunc = nullptr;
			AccumulateFunc = nullptr;
			FinalizeFunc = nullptr;
		}

		virtual void Blend(const void* A, const void* B, double /*Weight*/, void* Out) const override
		{
			const void* Src = (Mode == EPCGExABBlendingType::CopySource) ? B : A;
			FMemory::Memcpy(Out, Src, ValueSize);
		}

		virtual void BeginMulti(void* Accumulator, const void* /*InitialValue*/, PCGEx::FOpStats& OutTracker) const override
		{
			FMemory::Memzero(Accumulator, ValueSize);
			OutTracker.Count = 0;
			OutTracker.TotalWeight = 0;
		}

		virtual void Accumulate(const void* Source, void* Accumulator, double /*Weight*/) const override
		{
			// For unknown types, last-write-wins is the only safe semantic
			FMemory::Memcpy(Accumulator, Source, ValueSize);
		}

		virtual void EndMulti(void* /*Accumulator*/, double /*TotalWeight*/, int32 /*Count*/) const override {}

		virtual void Div(void* /*Value*/, double /*Divisor*/) const override {}
		virtual EPCGMetadataTypes GetWorkingType() const override { return EPCGMetadataTypes::Unknown; }
		virtual int32 GetValueSize() const override { return ValueSize; }
		virtual int32 GetValueAlignment() const override { return ValueAlignment; }
		virtual void InitDefault(void* Value) const override { FMemory::Memzero(Value, ValueSize); }
		virtual bool NeedsLifecycleManagement() const override { return false; }
		virtual void ConstructValue(void* Value) const override { FMemory::Memzero(Value, ValueSize); }
		virtual void DestroyValue(void* /*Value*/) const override {}
		virtual void CopyValue(const void* Src, void* Dst) const override { FMemory::Memcpy(Dst, Src, ValueSize); }
	};

	//
	// FBlendOperationFactory - Creates blend operations with runtime dispatch
	//
	// Single entry point for creating blend operations. Uses switch on type
	// to dispatch to the appropriate TBlendOperationImpl<T> constructor.
	//
	class PCGEXBLENDING_API FBlendOperationFactory
	{
	public:
		// Create a blend operation for runtime type and mode.
		// For unknown types, returns a FCopyOnlyBlendOperation using the provided size/alignment.
		// If InValueSize is 0 for an unknown type, returns nullptr.
		static TSharedPtr<IBlendOperation> Create(
			EPCGMetadataTypes WorkingType,
			EPCGExABBlendingType BlendMode,
			bool bResetForMultiBlend = true,
			int32 InValueSize = 0,
			int32 InValueAlignment = 1);

		// Compile-time factory for known type
		template <typename T>
		static TSharedPtr<IBlendOperation> CreateTyped(
			EPCGExABBlendingType BlendMode,
			bool bResetForMultiBlend = true)
		{
			return MakeShared<TBlendOperationImpl<T>>(BlendMode, bResetForMultiBlend);
		}
	};

	//
	// FBlenderPool - Caches blend operations for reuse
	//
	// Avoids repeated allocations by caching commonly used blend operations.
	// Thread-safe through use of shared pointers and critical section.
	//
	class PCGEXBLENDING_API FBlenderPool
	{
	public:
		// Get or create a blend operation.
		// For unknown types, InValueSize/InValueAlignment are forwarded to FCopyOnlyBlendOperation.
		// Copy-only operations are NOT cached (size-specific per attribute).
		TSharedPtr<IBlendOperation> Get(
			EPCGMetadataTypes WorkingType,
			EPCGExABBlendingType BlendMode,
			bool bResetForMultiBlend = true,
			int32 InValueSize = 0,
			int32 InValueAlignment = 1);

		// Clear all cached operations
		void Clear();

		// Singleton access
		static FBlenderPool& GetGlobal();

	private:
		// Cache key
		struct FKey
		{
			EPCGMetadataTypes Type;
			EPCGExABBlendingType Mode;
			bool bReset;

			bool operator==(const FKey& Other) const
			{
				return Type == Other.Type && Mode == Other.Mode && bReset == Other.bReset;
			}
		};

		friend uint32 GetTypeHash(const FKey& Key)
		{
			return HashCombine(
				GetTypeHash(static_cast<uint8>(Key.Type)),
				HashCombine(GetTypeHash(static_cast<uint8>(Key.Mode)), GetTypeHash(Key.bReset)));
		}

		TMap<FKey, TSharedPtr<IBlendOperation>> Cache;
		FCriticalSection CacheLock;
	};
}
