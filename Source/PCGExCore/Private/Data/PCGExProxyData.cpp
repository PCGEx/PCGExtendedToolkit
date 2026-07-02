// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExProxyData.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadataAttribute.h"

namespace PCGExData
{
#pragma region FProxyDescriptor

	void FProxyDescriptor::UpdateSubSelection()
	{
		SubSelection = FSubSelection(Selector);
	}

	bool FProxyDescriptor::SetFieldIndex(const int32 InFieldIndex)
	{
		if (!SubSelection.SetFieldIndex(InFieldIndex))
		{
			return false;
		}
		UpdateSubSelection();
		return true;
	}

	bool FProxyDescriptor::Capture(FPCGExContext* InContext, const FString& Path, const EIOSide InSide, const bool bRequired)
	{
		FPCGAttributePropertyInputSelector NewSelector;
		NewSelector.Update(Path);
		return Capture(InContext, NewSelector, InSide, bRequired);
	}

	bool FProxyDescriptor::Capture(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const bool bRequired)
	{
		const TSharedPtr<FFacade> InFacade = DataFacade.Pin();
		check(InFacade);

		bool bValid = true;
		Side = HasFlag(EProxyFlags::Constant) ? EIOSide::In : InSide;

		if (!TryGetTypeAndSource(InSelector, InFacade, RealType, Side))
		{
			if (bRequired)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, , InSelector)
			}
			bValid = false;
		}

		PointData = InFacade->Source->GetData(Side);
		Selector = InSelector.CopyAndFixLast(InFacade->Source->GetData(Side));

		UpdateSubSelection();
		WorkingType = SubSelection.GetSubType(RealType);

		// Cache the source attribute's Desc. Reset to default first so SourceDesc.IsValid()
		// returns false when the selector isn't an attribute (point property, extra property).
		SourceDesc = FPCGMetadataAttributeDesc{};
		if (Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			if (const UPCGData* InData = InFacade->Source->GetData(Side);
				InData && InData->Metadata)
			{
				// Domain-safe lookup: the selector may target @Data on data without an instantiated @Data domain.
				if (const FPCGMetadataAttributeBase* Attr = PCGExMetaHelpers::TryGetConstAttribute(
					InData->Metadata.Get(), PCGExMetaHelpers::GetAttributeIdentifier(Selector, InData)))
				{
					SourceDesc = Attr->GetAttributeDesc();
				}
			}
		}

		return bValid;
	}

	bool FProxyDescriptor::CaptureStrict(FPCGExContext* InContext, const FString& Path, const EIOSide InSide, const bool bRequired)
	{
		if (!Capture(InContext, Path, InSide, bRequired))
		{
			return false;
		}

		if (Side != InSide)
		{
			if (bRequired && !InContext->bQuietMissingAttributeError)
			{
				if (InSide == EIOSide::In)
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" does not exist on input."), FText::FromString(Path)));
				}
				else
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" does not exist on output."), FText::FromString(Path)));
				}
			}

			return false;
		}

		return true;
	}

	bool FProxyDescriptor::CaptureStrict(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const bool bRequired)
	{
		if (!Capture(InContext, InSelector, InSide, bRequired))
		{
			return false;
		}

		if (Side != InSide)
		{
			if (bRequired && !InContext->bQuietMissingAttributeError)
			{
				if (InSide == EIOSide::In)
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" does not exist on input."), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(InSelector))));
				}
				else
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" does not exist on output."), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(InSelector))));
				}
			}

			return false;
		}

		return true;
	}

	bool FProxyDescriptor::Capture_Unsafe(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const EPCGMetadataTypes InKnownRealType, const FPCGMetadataAttributeDesc* InKnownSourceDesc)
	{
		const TSharedPtr<FFacade> InFacade = DataFacade.Pin();
		check(InFacade);

		// Trust the caller's type and side: no TryGetTypeAndSource probe.
		Side = HasFlag(EProxyFlags::Constant) ? EIOSide::In : InSide;
		RealType = InKnownRealType;

		PointData = InFacade->Source->GetData(Side);
		Selector = InSelector.CopyAndFixLast(InFacade->Source->GetData(Side));

		UpdateSubSelection();
		WorkingType = SubSelection.GetSubType(RealType);

		// SourceDesc supplied by the caller -- skip the metadata lookup. Gate on attribute selection
		// to mirror Capture(), which leaves SourceDesc default for point/extra-property selectors.
		SourceDesc = FPCGMetadataAttributeDesc{};
		if (InKnownSourceDesc && Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			SourceDesc = *InKnownSourceDesc;
		}

		return RealType != EPCGMetadataTypes::Unknown;
	}

	bool FProxyDescriptor::CaptureStrict_Unsafe(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const EPCGMetadataTypes InKnownRealType, const FPCGMetadataAttributeDesc* InKnownSourceDesc)
	{
		// The strict side-match guarantee is the caller's assertion in the unsafe path: the side is
		// taken as given and never flipped, so this just forwards to Capture_Unsafe.
		return Capture_Unsafe(InContext, InSelector, InSide, InKnownRealType, InKnownSourceDesc);
	}

	uint32 GetSelectorTypeHash(const FPCGAttributePropertyInputSelector& Selector)
	{
		EPCGAttributePropertySelection Selection = Selector.GetSelection();
		uint32 Hash = HashCombine(GetTypeHash(Selection), GetTypeHash(Selector.GetDomainName()));

		switch (Selection)
		{
		case EPCGAttributePropertySelection::Attribute:
			Hash = HashCombine(Hash, GetTypeHash(Selector.GetAttributeName()));
			break;
		case EPCGAttributePropertySelection::Property:
			Hash = HashCombine(Hash, GetTypeHash(Selector.GetPropertyName()));
			break;
		case EPCGAttributePropertySelection::ExtraProperty:
			Hash = HashCombine(Hash, GetTypeHash(Selector.GetExtraProperty()));
			break;
		default:
			break;
		}

		for (const FString& ExtraName : Selector.GetExtraNames())
		{
			Hash = HashCombine(Hash, GetTypeHash(ExtraName));
		}

		return Hash;
	}

	uint32 GetTypeHash(const FProxyDescriptor& D)
	{
		uint32 Hash = 0;

		Hash = HashCombineFast(Hash, GetSelectorTypeHash(D.Selector));
		Hash = HashCombineFast(Hash, GetTypeHash(D.SubSelection));
		Hash = HashCombineFast(Hash, static_cast<uint32>(D.Side));
		Hash = HashCombineFast(Hash, static_cast<uint32>(D.Role));
		Hash = HashCombineFast(Hash, static_cast<uint32>(D.RealType));
		Hash = HashCombineFast(Hash, static_cast<uint32>(D.WorkingType));
		Hash = HashCombineFast(Hash, static_cast<uint32>(D.Flags));
		Hash = HashCombineFast(Hash, D.PointData ? D.PointData->GetUniqueID() : 0);

		return Hash;
	}

#pragma endregion

#pragma region IBufferProxy

	IBufferProxy::IBufferProxy(EPCGMetadataTypes InRealType, EPCGMetadataTypes InWorkingType)
		: RealType(InRealType)
		  , WorkingType(InWorkingType == EPCGMetadataTypes::Unknown ? InRealType : InWorkingType)
		  , WorkingToReal(PCGExTypeOps::FConversionTable::GetConversionFn(InWorkingType == EPCGMetadataTypes::Unknown ? InRealType : InWorkingType, InRealType))
		  , RealToWorking(PCGExTypeOps::FConversionTable::GetConversionFn(InRealType, InWorkingType == EPCGMetadataTypes::Unknown ? InRealType : InWorkingType))
	{
		// Get type ops from registry
		RealOps = PCGExTypeOps::FTypeOpsRegistry::Get(RealType);
		WorkingOps = PCGExTypeOps::FTypeOpsRegistry::Get(WorkingType);

		// Cache whether working type needs lifecycle management
		bWorkingTypeNeedsLifecycle = TypeTraits::NeedsLifecycleManagement(WorkingType);
	}

	bool IBufferProxy::Validate(const FProxyDescriptor& InDescriptor) const
	{
		return RealType == InDescriptor.RealType && WorkingType == InDescriptor.WorkingType;
	}

	void IBufferProxy::SetSubSelection(const FSubSelection& InSubSelection,
	                                   const FPCGMetadataAttributeDesc* SourceDesc)
	{
		bWantsSubSelection = InSubSelection.HasSelection();
		if (bWantsSubSelection)
		{
			CachedSubSelection.Initialize(InSubSelection, RealType, WorkingType, SourceDesc);
		}
	}

	void IBufferProxy::InitForRole(EProxyRole InRole)
	{
		// Default: no-op. Override in property proxies.
	}

	// Converting read implementations - Now using FScopedTypedValue for safety
#define PCGEX_CONVERTING_READ_IMPL(_TYPE, _NAME, ...) \
	_TYPE IBufferProxy::ReadAs##_NAME(const int32 Index) const \
	{ \
		PCGExTypes::FScopedTypedValue WorkingValue(WorkingType); \
		GetVoid(Index, WorkingValue.GetRaw()); \
		constexpr EPCGMetadataTypes TargetType = PCGExTypes::TTraits<_TYPE>::Type; \
		if (TargetType == WorkingType) \
		{ \
			if constexpr (TypeTraits::TIsComplexType<_TYPE>) { return WorkingValue.As<_TYPE>(); }\
			else { return *reinterpret_cast<const _TYPE*>(WorkingValue.GetRaw()); }\
		} \
		_TYPE Result{}; \
		PCGExTypeOps::FConversionTable::Convert(WorkingType, WorkingValue.GetRaw(), TargetType, &Result);\
		return Result; \
	}
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_CONVERTING_READ_IMPL)

#undef PCGEX_CONVERTING_READ_IMPL

	int32 IBufferProxy::GetValueSize() const
	{
		if (WorkingOps)
		{
			return WorkingOps->GetTypeSize();
		}
		return PCGExTypes::FScopedTypedValue::GetTypeSize(WorkingType);
	}

	int32 IBufferProxy::GetValueAlignment() const
	{
		if (WorkingOps)
		{
			return WorkingOps->GetTypeAlignment();
		}
		return 1;
	}

#pragma endregion

	TSharedPtr<IBufferProxy> IBufferProxyPool::GetOrCreate(
		const FProxyDescriptor& Descriptor,
		TFunctionRef<TSharedPtr<IBufferProxy>()> Factory)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IBufferProxyPool::GetOrCreate);

		const uint64 Key = GetTypeHash(Descriptor);

		// In-side descriptors read shared, stable upstream input: retain the proxy strongly so
		// it survives for the pool owner's lifetime and is reused by every consumer. Out-side
		// descriptors are this data's own transient output: keep them weak (die with the consumer).
		const bool bRetain = Descriptor.Side == EIOSide::In;

		// Read-locked fast path: cache hits proceed in parallel across threads.
		// Critical for hot descriptors requested many times (e.g. shared blending
		// inputs across N IOs); without this, every repeat lookup would serialize on
		// the shard's write lock and sharding alone wouldn't help.
		//
		// We Pin the weak ref while still holding the shard read lock -- a concurrent
		// slow-path writer to the same shard can otherwise rehash the TMap or tear
		// the TWeakPtr's two-pointer assignment, invalidating any pointer we'd held
		// past the lock.
		TSharedPtr<IBufferProxy> Pinned;
		ProxyMap.ReadOrSkip(Key, [&](const FProxyPoolEntry& Slot)
		{
			Pinned = Slot.Weak.Pin();
		});
		if (Pinned) { return Pinned; }

		// Slow path: shard write lock, re-check (another thread may have populated
		// between our read unlock and the write acquire), then run Factory under lock.
		TSharedPtr<IBufferProxy> Result;
		ProxyMap.FindOrAddAndUpdate(
			Key,
			[&](FProxyPoolEntry& Slot)
			{
				if (TSharedPtr<IBufferProxy> Existing = Slot.Weak.Pin())
				{
					Result = Existing;
					if (bRetain && !Slot.Strong) { Slot.Strong = Existing; }
					return;
				}

				Result = Factory();
				if (Result)
				{
					Slot.Weak = Result;
					if (bRetain) { Slot.Strong = Result; }
				}
			});
		return Result;
	}
}
