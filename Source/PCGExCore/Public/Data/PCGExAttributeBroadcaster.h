// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <type_traits>

#include "CoreMinimal.h"
#include "PCGExSubSelection.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Types/PCGExTypeTraits.h"

class UPCGMetadata;
struct FPCGContext;
struct FPCGContextHandle;
class FPCGMetadataAttributeBase;
struct FPCGExContext;

namespace PCGExMT
{
	struct FScope;
}

struct FPCGExTaggedData;

namespace PCGExData
{
	struct FElement;
	class FPointIO;
	class IDataValue;
	class FPointIOCollection;
	class FFacade;
}

namespace PCGExData
{
	class PCGEXCORE_API IAttributeBroadcaster : public TSharedFromThis<IAttributeBroadcaster>
	{
	public:
		struct PCGEXCORE_API FAttributeProcessingInfos
		{
			bool bIsValid = false;
			bool bIsDataDomain = false;

			FAttributeIdentity SourceIdentity = FAttributeIdentity();
			FAttributeIdentity TargetIdentity = FAttributeIdentity();

			const FPCGMetadataAttributeBase* Attribute = nullptr;

			FPCGAttributePropertyInputSelector Selector;
			FSubSelection SubSelection = FSubSelection();

			FAttributeProcessingInfos() = default;

			FAttributeProcessingInfos(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector);
			FAttributeProcessingInfos(const UPCGData* InData, const FName InAttributeName);

			operator const FPCGMetadataAttributeBase*() const
			{
				return Attribute;
			}

			operator EPCGMetadataTypes() const;

			operator EPCGAttributePropertySelection() const
			{
				return Selector.GetSelection();
			}

			operator EPCGPointProperties() const
			{
				return Selector.GetPointProperty();
			}

			operator EPCGExtraProperties() const
			{
				return Selector.GetExtraProperty();
			}

		protected:
			void Init(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector);
		};

		FAttributeProcessingInfos ProcessingInfos = FAttributeProcessingInfos();
		IAttributeBroadcaster() = default;
		virtual ~IAttributeBroadcaster() = default;

		IAttributeBroadcaster(IAttributeBroadcaster&&) = default;
		IAttributeBroadcaster& operator=(IAttributeBroadcaster&&) = default;

		IAttributeBroadcaster(const IAttributeBroadcaster&) = delete;
		IAttributeBroadcaster& operator=(const IAttributeBroadcaster&) = delete;

		const FPCGMetadataAttributeBase* GetAttribute() const;
		virtual EPCGMetadataTypes GetMetadataType() const;
		virtual FName GetName() const;

	protected:
		// Unset when prepared from raw data: failed reads are then logged without a graph.
		TWeakPtr<FPCGContextHandle> ContextHandle;
		FName SourceClassName = NAME_None;

		// Plain int32 driven by FPlatformAtomics, so the class stays movable.
		mutable int32 ReadFailureReported = 0;

		// Warns once per broadcaster. Callable from any thread.
		void ReportReadFailure() const;
	};

	template <typename T>
	class PCGEXCORE_API TAttributeBroadcaster : public IAttributeBroadcaster
	{
	protected:
		using Traits = PCGExTypes::TTraits<T>;

		TSharedPtr<IPCGAttributeAccessorKeys> Keys;
		TUniquePtr<const IPCGAttributeAccessor> InternalAccessor;

		TSharedPtr<IDataValue> DataValue;
		T TypedDataValue = T{};

		// False once a read through InternalAccessor has failed: every read then serves T{} without touching it.
		bool bReadable = true;

		bool ApplySelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData);

		// One-element read, run once keys and accessor are both set. GetRange fails the same way for one element
		// as for a scope (keys the accessor can't use, or no conversion to T), so this settles bReadable up front.
		void ProbeRead();

	public:
		FORCEINLINE TAttributeBroadcaster() = default;

		virtual FName GetName() const override;

		TArray<T> Values;
		mutable T Min = T{};
		mutable T Max = T{};

		bool bEnabled = true;

		bool IsUsable(int32 NumEntries);

		bool Prepare(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO);
		bool Prepare(const FName& InName, const TSharedRef<FPointIO>& InPointIO);
		bool Prepare(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO);

		bool PrepareForSingleFetch(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys = nullptr);
		bool PrepareForSingleFetch(const FName& InName, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys = nullptr);
		bool PrepareForSingleFetch(const FPCGAttributeIdentifier& InIdentifier, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys = nullptr);

		bool PrepareForSingleFetch(const FPCGAttributePropertyInputSelector& InSelector, const FPCGExTaggedData& InData);
		bool PrepareForSingleFetch(const FName& InName, const FPCGExTaggedData& InData);
		bool PrepareForSingleFetch(const FPCGAttributeIdentifier& InIdentifier, const FPCGExTaggedData& InData);

		/**
		 * Build and validate a property/attribute accessor for the selected
		 * @param Dump
		 * @param Scope
		 */
		void Fetch(TArray<T>& Dump, const PCGExMT::FScope& Scope);

		/**
		 * Slice-local variant of Fetch. Dest must be sized to Scope.Count and writes
		 * start at Dest[0] (not Dest[Scope.Start]). Useful when the caller only needs
		 * the scope's range, not a full-size buffer.
		 */
		void FetchSlice(TArrayView<T> Dest, const PCGExMT::FScope& Scope);

		/**
		 * Build and validate a property/attribute accessor for the selected
		 * @param Dump
		 * @param bCaptureMinMax
		 * @param OutMin
		 * @param OutMax
		 */
		void GrabAndDump(TArray<T>& Dump, const bool bCaptureMinMax, T& OutMin, T& OutMax);

		void GrabUniqueValues(TSet<T>& OutUniqueValues);

		void Grab(const bool bCaptureMinMax = false);

		T FetchSingle(const FElement& Element, const T& Fallback) const;
		bool TryFetchSingle(const FElement& Element, T& OutValue) const;
		virtual EPCGMetadataTypes GetMetadataType() const override;
	};

	PCGEXCORE_API TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FName& InName, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

	PCGEXCORE_API TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

	PCGEXCORE_API TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

	// Raw-data variant: builds a broadcaster from a UPCGData (point data OR attribute set) without an FPointIO. Always single-fetch.
	PCGEXCORE_API TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData);

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FName& InName, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch = false);

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FName& InName, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch); \
extern template TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch); \
extern template TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion
}
