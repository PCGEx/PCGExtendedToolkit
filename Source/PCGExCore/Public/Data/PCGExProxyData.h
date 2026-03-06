// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.h"
#include "Types/PCGExTypes.h"
#include "Data/PCGExSubSelection.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Types/PCGExTypeOps.h"
#include "UObject/Object.h"
#include "Data/PCGExCachedSubSelection.h"
#include "Misc/ScopeRWLock.h"

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
			case EPCGMetadataTypes::SoftClassPath: return true;
			default: return false;
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

		// Element size/alignment for generic types (0 = derived from type enum)
		int32 ValueSize = 0;
		int32 ValueAlignment = 1;

		TWeakPtr<FFacade> DataFacade;
		const UPCGBasePointData* PointData = nullptr;

		EProxyFlags Flags = EProxyFlags::None;
		FORCEINLINE bool HasFlag(const EProxyFlags InFlags) const { return EnumHasAnyFlags(Flags, InFlags); }
		FORCEINLINE void AddFlags(const EProxyFlags InFlags) { EnumAddFlags(Flags, InFlags); }

		FProxyDescriptor() = default;

		explicit FProxyDescriptor(const TSharedPtr<FFacade>& InDataFacade, const EProxyRole InRole = EProxyRole::Read)
			: Role(InRole), DataFacade(InDataFacade)
		{
		}

		~FProxyDescriptor() = default;

		void UpdateSubSelection();
		bool SetFieldIndex(const int32 InFieldIndex);

		bool Capture(FPCGExContext* InContext, const FString& Path, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);
		bool Capture(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);

		bool CaptureStrict(FPCGExContext* InContext, const FString& Path, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);
		bool CaptureStrict(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide = EIOSide::Out, const bool bRequired = true);

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
		virtual TSharedPtr<IBuffer> GetBuffer() const { return nullptr; }
		virtual bool EnsureReadable() const { return true; }

		// SubSelection configuration
		void SetSubSelection(const FSubSelection& InSubSelection);

		// Role-specific initialization
		virtual void InitForRole(EProxyRole InRole);

		//
		// Type-erased value access - core methods
		//
		virtual void GetVoid(const int32 Index, void* OutValue) const = 0;
		virtual void SetVoid(const int32 Index, const void* Value) const = 0;
		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const { GetVoid(Index, OutValue); }

		// Hash computation
		virtual PCGExValueHash ReadValueHash(const int32 Index) const = 0;

		//
		// Type information accessors
		//
		FORCEINLINE const PCGExTypeOps::ITypeOpsBase* GetRealOps() const { return RealOps; }
		FORCEINLINE const PCGExTypeOps::ITypeOpsBase* GetWorkingOps() const { return WorkingOps; }
		FORCEINLINE bool HasSubSelection() const { return bWantsSubSelection; }
		FORCEINLINE bool WorkingTypeNeedsLifecycle() const { return bWorkingTypeNeedsLifecycle; }

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
				PCGExTypes::FScopedTypedValue WorkingValue(WorkingType);
				GetVoid(Index, WorkingValue.GetRaw());

				if (RequestedType == WorkingType) { return WorkingValue.As<T>(); }

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
				PCGExTypes::FScopedTypedValue WorkingValue(WorkingType);
				GetVoid(Index, WorkingValue.GetRaw());

				if (RequestedType == WorkingType) { return *reinterpret_cast<const T*>(WorkingValue.GetRaw()); }

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
			PCGExTypes::FScopedTypedValue WorkingValue(WorkingType);
			PCGExTypeOps::FConversionTable::Convert(ValueType, &Value, WorkingType, WorkingValue.GetRaw());
			SetVoid(Index, WorkingValue.GetRaw());
		}

		template <typename T>
		T GetCurrent(const int32 Index) const
		{
			constexpr EPCGMetadataTypes RequestedType = PCGExTypes::TTraits<T>::Type;

			// Always use scoped value for safety
			PCGExTypes::FScopedTypedValue WorkingValue(WorkingType);
			GetCurrentVoid(Index, WorkingValue.GetRaw());

			if (RequestedType == WorkingType)
			{
				if constexpr (TypeTraits::TIsComplexType<T>) { return WorkingValue.As<T>(); }
				else { return *reinterpret_cast<const T*>(WorkingValue.GetRaw()); }
			}

			T Result{};
			PCGExTypeOps::FConversionTable::Convert(WorkingType, WorkingValue.GetRaw(), RequestedType, &Result);
			return Result;
		}

		// Converting read methods - SAFE versions
#define PCGEX_CONVERTING_READ(_TYPE, _NAME, ...) virtual _TYPE ReadAs##_NAME(const int32 Index) const;
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_CONVERTING_READ)
#undef PCGEX_CONVERTING_READ

		// Helper to create a scoped value for this proxy's working type
		FORCEINLINE PCGExTypes::FScopedTypedValue CreateScopedWorkingValue() const
		{
			return PCGExTypes::FScopedTypedValue(WorkingType);
		}
	};

	class PCGEXCORE_API IBufferProxyPool : public TSharedFromThis<IBufferProxyPool>
	{
		mutable FRWLock MapLock;
		TMap<uint32, TWeakPtr<IBufferProxy>> ProxyMap;

	public:
		IBufferProxyPool() = default;

		TSharedPtr<IBufferProxy> TryGet(const FProxyDescriptor& Descriptor);
		void Add(const FProxyDescriptor& Descriptor, const TSharedPtr<IBufferProxy>& Proxy);
	};
}
