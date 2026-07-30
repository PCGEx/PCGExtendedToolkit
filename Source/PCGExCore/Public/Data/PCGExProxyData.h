// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/PCGExCachedSubSelection.h"
#include "Data/PCGExCachedSubSelection.h"
#include "Data/PCGExSubSelection.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Misc/ScopeRWLock.h"
#include "Types/PCGExTypeOps.h"
#include "Types/PCGExTypeOps.h"
#include "Types/PCGExTypes.h"
#include "UObject/Object.h"
#include "UObject/Object.h"

struct FPCGExContext;
class UPCGBasePointData;

template <typename T>
class FPCGMetadataAttribute;

namespace PCGExData
{
	class IBuffer;

	template <typename T>
	class TBuffer;

	enum class EProxyRole : uint8
	{
		Read,
		Write
	};


	enum class EProxyFlags : uint8
	{
		None     = 0,
		Direct   = 1 << 0,
		Constant = 1 << 1,
		Raw      = 1 << 2,
		Shared   = 1 << 3,
	};

	ENUM_CLASS_FLAGS(EProxyFlags)

	//
	// Type traits for identifying non-trivially-copyable types
	//
	namespace TypeTraits
	{
		// Returns true if the type needs special lifecycle handling
		FORCEINLINE constexpr bool NeedsLifecycleManagement(EPCGMetadataTypes Type)
		{
			switch (Type)
			{
			case EPCGMetadataTypes::String:
			case EPCGMetadataTypes::Name:
			case EPCGMetadataTypes::SoftObjectPath:
			case EPCGMetadataTypes::SoftClassPath:
			case EPCGMetadataTypes::Text:
				return true;
			default:
				return false;
			}
		}

		template <typename T>
		constexpr bool TIsComplexType = !std::is_trivially_copyable_v<T>;
	}

	//
	// FProxyDescriptor - Describes a data source for proxy creation
	//
	struct PCGEXCORE_API FProxyDescriptor
	{
		FPCGAttributePropertyInputSelector Selector;
		FSubSelection SubSelection;

		EIOSide Side = EIOSide::In;
		EProxyRole Role = EProxyRole::Read;

		EPCGMetadataTypes RealType = EPCGMetadataTypes::Unknown;
		EPCGMetadataTypes WorkingType = EPCGMetadataTypes::Unknown;

		// Cached attribute descriptor, populated by Capture() when the selector resolves to
		// an actual attribute (point/extra-property selectors leave this default-constructed).
		// Check SourceDesc.IsValid() before relying on its fields. Used by container-aware
		// accessors in the compiled chain (FContainerIndex / FContainerCount); element size /
		// alignment for the proxy itself is read from the underlying buffer via IBuffer::GetValueSize.
		FPCGMetadataAttributeDesc SourceDesc;

		TWeakPtr<FFacade> DataFacade;
		const UPCGBasePointData* PointData = nullptr;

		EProxyFlags Flags = EProxyFlags::None;
		FORCEINLINE bool HasFlag(const EProxyFlags InFlags) const
		{
			return EnumHasAnyFlags(Flags, InFlags);
		}

		FORCEINLINE void AddFlags(const EProxyFlags InFlags)
		{
			EnumAddFlags(Flags, InFlags);
		}

		FProxyDescriptor() = default;

		explicit FProxyDescriptor(const TSharedPtr<FFacade>& InDataFacade, const EProxyRole InRole = EProxyRole::Read)
			: Role(InRole)
			  , DataFacade(InDataFacade)
		{
		}

		~FProxyDescriptor() = default;

		void UpdateSubSelection();
		bool SetFieldIndex(const int32 InFieldIndex);

		bool Capture(FPCGExContext* InContext, const FString& Path, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);
		bool Capture(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);

		bool CaptureStrict(FPCGExContext* InContext, const FString& Path, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);
		bool CaptureStrict(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);

		// Validation-skipping fast paths for callers that ALREADY know the attribute's real type
		// (e.g. blend init, where the type comes from FAttributeIdentity). They trust the caller's
		// InKnownRealType and InSide -- no TryGetTypeAndSource probe, no SourceDesc metadata lookup --
		// so they MUST only be used where type and side are known-correct. Selector fixup,
		// SubSelection and WorkingType are still resolved exactly as Capture() does. Pass a null
		// InKnownSourceDesc for non-attribute selectors (mirrors Capture leaving SourceDesc default).
		// CaptureStrict_Unsafe is provided for call-site symmetry; since the side is trusted (never
		// flipped), the strict side-match it guarantees is the caller's assertion and it simply
		// forwards to Capture_Unsafe.
		bool Capture_Unsafe(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const EPCGMetadataTypes InKnownRealType, const FPCGMetadataAttributeDesc* InKnownSourceDesc = nullptr);
		bool CaptureStrict_Unsafe(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const EPCGMetadataTypes InKnownRealType, const FPCGMetadataAttributeDesc* InKnownSourceDesc = nullptr);

		friend uint32 GetTypeHash(const FProxyDescriptor& D);
	};

	//
	// IBufferProxy - Type-erased interface for all proxy buffer operations
	//
	// Uses ITypeOpsBase* for runtime type dispatch - eliminates template explosion
	// while providing full conversion/blending capabilities through the type ops system.
	//
	class PCGEXCORE_API IBufferProxy : public TSharedFromThis<IBufferProxy>
	{
	protected:
		// SubSelection support
		bool bWantsSubSelection = false;
		FCachedSubSelection CachedSubSelection;

		// Type operations from registry - provides all conversion & blending
		const PCGExTypeOps::ITypeOpsBase* RealOps = nullptr;
		const PCGExTypeOps::ITypeOpsBase* WorkingOps = nullptr;

		// Cached flag for whether working type needs lifecycle management
		bool bWorkingTypeNeedsLifecycle = false;

	public:
		// Point data reference for property proxies
		UPCGBasePointData* Data = nullptr;

		// Type information
		const EPCGMetadataTypes RealType;
		const EPCGMetadataTypes WorkingType;

		// Direct conversion function pointers (from FConversionTable)
		const PCGExTypeOps::FConvertFn WorkingToReal;
		const PCGExTypeOps::FConvertFn RealToWorking;

		explicit IBufferProxy(
			EPCGMetadataTypes InRealType = EPCGMetadataTypes::Unknown,
			EPCGMetadataTypes InWorkingType = EPCGMetadataTypes::Unknown);

		virtual ~IBufferProxy() = default;

		// Validation
		virtual bool Validate(const FProxyDescriptor& InDescriptor) const;

		// Buffer access (for attribute proxies)
		virtual TSharedPtr<IBuffer> GetBuffer() const
		{
			return nullptr;
		}

		virtual bool EnsureReadable() const
		{
			return true;
		}

		// SubSelection configuration. SourceDesc is forwarded to the compiled
		// chain so container-aware steps can classify against the attribute's
		// container-ness. Pass nullptr for non-attribute sources.
		void SetSubSelection(const FSubSelection& InSubSelection,
		                     const FPCGMetadataAttributeDesc* SourceDesc = nullptr);

		// Role-specific initialization
		virtual void InitForRole(EProxyRole InRole);

		//
		// Type-erased value access - core methods
		//
		virtual void GetVoid(const int32 Index, void* OutValue) const = 0;
		virtual void SetVoid(const int32 Index, const void* Value) const = 0;

		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const
		{
			GetVoid(Index, OutValue);
		}

		// Hash computation
		virtual PCGExValueHash ReadValueHash(const int32 Index) const = 0;

		//
		// Type information accessors
		//
		FORCEINLINE const PCGExTypeOps::ITypeOpsBase* GetRealOps() const
		{
			return RealOps;
		}

		FORCEINLINE const PCGExTypeOps::ITypeOpsBase* GetWorkingOps() const
		{
			return WorkingOps;
		}

		FORCEINLINE bool HasSubSelection() const
		{
			return bWantsSubSelection;
		}

		FORCEINLINE bool WorkingTypeNeedsLifecycle() const
		{
			return bWorkingTypeNeedsLifecycle;
		}

		// Value size/alignment for blend operation sizing.
		// Returns the size from WorkingOps if available, otherwise falls back to descriptor or FScopedTypedValue::GetTypeSize.
		virtual int32 GetValueSize() const;
		virtual int32 GetValueAlignment() const;

		//
		// Convenience typed accessors - SAFE versions with proper lifecycle
		//

		template <typename T>
		T Get(const int32 Index) const
		{
			constexpr EPCGMetadataTypes RequestedType = PCGExTypes::TTraits<T>::Type;

			if constexpr (TypeTraits::TIsComplexType<T>)
			{
				// Complex type - use scoped value for proper lifecycle
				PCGExTypes::FScopedTypedValue WorkingValue = CreateScopedWorkingValue();
				GetVoid(Index, WorkingValue.GetRaw());

				if (RequestedType == WorkingType)
				{
					return WorkingValue.As<T>();
				}

				T Result{};
				PCGExTypeOps::FConversionTable::Convert(WorkingType, WorkingValue.GetRaw(), RequestedType, &Result);
				return Result;
			}
			else
			{
				// POD type - can use direct access if types match
				if (RequestedType == WorkingType && !bWorkingTypeNeedsLifecycle)
				{
					// Fast path: direct read for matching POD types
					T Result{};
					GetVoid(Index, &Result);
					return Result;
				}

				// Working type might be complex, use scoped value
				PCGExTypes::FScopedTypedValue WorkingValue = CreateScopedWorkingValue();
				GetVoid(Index, WorkingValue.GetRaw());

				if (RequestedType == WorkingType)
				{
					return *reinterpret_cast<const T*>(WorkingValue.GetRaw());
				}

				T Result{};
				PCGExTypeOps::FConversionTable::Convert(WorkingType, WorkingValue.GetRaw(), RequestedType, &Result);
				return Result;
			}
		}

		template <typename T>
		void Set(const int32 Index, const T& Value) const
		{
			constexpr EPCGMetadataTypes ValueType = PCGExTypes::TTraits<T>::Type;

			if (ValueType == WorkingType)
			{
				// Types match - direct set
				SetVoid(Index, &Value);
				return;
			}

			// Need conversion - use scoped value for working type
			PCGExTypes::FScopedTypedValue WorkingValue = CreateScopedWorkingValue();
			PCGExTypeOps::FConversionTable::Convert(ValueType, &Value, WorkingType, WorkingValue.GetRaw());
			SetVoid(Index, WorkingValue.GetRaw());
		}

		template <typename T>
		T GetCurrent(const int32 Index) const
		{
			constexpr EPCGMetadataTypes RequestedType = PCGExTypes::TTraits<T>::Type;

			// Always use scoped value for safety
			PCGExTypes::FScopedTypedValue WorkingValue = CreateScopedWorkingValue();
			GetCurrentVoid(Index, WorkingValue.GetRaw());

			if (RequestedType == WorkingType)
			{
				if constexpr (TypeTraits::TIsComplexType<T>)
				{
					return WorkingValue.As<T>();
				}
				else
				{
					return *reinterpret_cast<const T*>(WorkingValue.GetRaw());
				}
			}

			T Result{};
			PCGExTypeOps::FConversionTable::Convert(WorkingType, WorkingValue.GetRaw(), RequestedType, &Result);
			return Result;
		}

		// Converting read methods - SAFE versions
#define PCGEX_CONVERTING_READ(_TYPE, _NAME, ...) virtual _TYPE ReadAs##_NAME(const int32 Index) const;
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_CONVERTING_READ)
#undef PCGEX_CONVERTING_READ

		// Scoped storage for the void accessors: property-aware for property-backed proxies (which
		// perform no Real->Working conversion), plain WorkingType otherwise. Always use this -- not
		// FScopedTypedValue(WorkingType) -- for storage handed to GetVoid/SetVoid/GetCurrentVoid.
		PCGExTypes::FScopedTypedValue CreateScopedWorkingValue() const;
	};

	// One pool slot. Weak is always set and drives the liveness check; Strong is set only
	// for retained (In-side) entries so a shared input proxy survives for the owning pool's
	// lifetime and is reused across every consumer instead of being rebuilt per consumer.
	struct FProxyPoolEntry
	{
		TWeakPtr<IBufferProxy> Weak;
		TSharedPtr<IBufferProxy> Strong;
	};

	// Get-or-create cache for buffer proxies, keyed by FProxyDescriptor hash.
	//
	// Instances live in two places (see GetProxyBuffer routing): one per FFacade (the primary
	// home -- a target facade's pool is touched only by its own compiling thread, a source
	// facade's pool is shared across consumers of that input) and one on FPCGExContext as the
	// fallback for the rare descriptor that has no pinnable facade. Per-facade pools eliminate
	// the cross-instance lock contention and unbounded growth of the prior single context-wide
	// map, and free their entries when the facade dies.
	//
	// 8-way sharded map: descriptors route to one of 8 (shard, lock) pairs by hash, so two
	// threads requesting different descriptors only contend on a hash collision. Per-facade
	// contention is low, so 8 shards keep the per-facade footprint small while still letting
	// concurrent consumers of a shared source facade (clusters compiling in parallel) proceed
	// without serializing on a single lock.
	class PCGEXCORE_API IBufferProxyPool : public TSharedFromThis<IBufferProxyPool>
	{
		PCGExMT::TH64MapShards<FProxyPoolEntry, 8> ProxyMap;

	public:
		IBufferProxyPool() = default;

		// Atomic get-or-create on the descriptor's shard:
		//   - Find under shard read lock first (fast path for cache hits).
		//   - Miss: take shard write lock, re-check, run Factory, register.
		// Factory's side effects (e.g. InitForRole → AllocateProperties on shared
		// point data) are serialized per descriptor, so two threads racing for the
		// same descriptor never double-construct or double-initialize. Unrelated
		// descriptors in different shards proceed in parallel.
		//
		// Retention: In-side descriptors (shared, stable upstream input) are held strongly so
		// they outlive any single consumer and are reused; Out-side descriptors (this data's own
		// transient output) are held weakly and die with the consumer that created them.
		//
		// Constraint: Factory must NOT call back into the pool with a descriptor that
		// hashes to the same shard (would deadlock on the shard's write lock). Current
		// proxy creation helpers do not call GetOrCreate recursively at all.
		TSharedPtr<IBufferProxy> GetOrCreate(
			const FProxyDescriptor& Descriptor,
			TFunctionRef<TSharedPtr<IBufferProxy>()> Factory);
	};
}
