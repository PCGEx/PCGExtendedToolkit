// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExProxyData.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGPointArrayData.h"

namespace PCGExData
{
#pragma region FProxyDescriptor

	void FProxyDescriptor::UpdateSubSelection()
	{
		SubSelection = FSubSelection(Selector);
	}

	bool FProxyDescriptor::SetFieldIndex(const int32 InFieldIndex)
	{
		if (!SubSelection.SetFieldIndex(InFieldIndex)) { return false; }
		UpdateSubSelection();
		return true;
	}

	bool FProxyDescriptor::Capture(FPCGExContext* InContext, const FString& Path, const EIOSide InSide, const bool bRequired)
	{
		const TSharedPtr<FFacade> InFacade = DataFacade.Pin();
		check(InFacade);

		bool bValid = true;

		Selector = FPCGAttributePropertyInputSelector();
		Selector.Update(Path);

		Side = InSide;

		if (!TryGetTypeAndSource(Selector, InFacade, RealType, Side))
		{
			if (bRequired) { PCGEX_LOG_INVALID_SELECTOR_C(InContext, , Selector) }
			bValid = false;
		}

		Selector = Selector.CopyAndFixLast(InFacade->Source->GetData(Side));

		UpdateSubSelection();
		WorkingType = SubSelection.GetSubType(RealType);

		// Derive size/alignment for generic types that GetTypeSize doesn't know about
		if (bValid && PCGExTypes::FScopedTypedValue::GetTypeSize(WorkingType) == 0)
		{
			ValueSize = PCGExTypes::GetElementSizeFromType(WorkingType);
			ValueAlignment = PCGExTypes::GetElementAlignmentFromType(WorkingType);
		}

		return bValid;
	}

	bool FProxyDescriptor::Capture(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, const EIOSide InSide, const bool bRequired)
	{
		const TSharedPtr<FFacade> InFacade = DataFacade.Pin();
		check(InFacade);

		bool bValid = true;
		Side = HasFlag(EProxyFlags::Constant) ? EIOSide::In : InSide;

		if (!TryGetTypeAndSource(InSelector, InFacade, RealType, Side))
		{
			if (bRequired) { PCGEX_LOG_INVALID_SELECTOR_C(InContext, , InSelector) }
			bValid = false;
		}

		PointData = InFacade->Source->GetData(Side);
		Selector = InSelector.CopyAndFixLast(InFacade->Source->GetData(Side));

		UpdateSubSelection();
		WorkingType = SubSelection.GetSubType(RealType);

		// Derive size/alignment for generic types that GetTypeSize doesn't know about
		if (bValid && PCGExTypes::FScopedTypedValue::GetTypeSize(WorkingType) == 0)
		{
			ValueSize = PCGExTypes::GetElementSizeFromType(WorkingType);
			ValueAlignment = PCGExTypes::GetElementAlignmentFromType(WorkingType);
		}

		return bValid;
	}

	bool FProxyDescriptor::CaptureStrict(FPCGExContext* InContext, const FString& Path, const EIOSide InSide, const bool bRequired)
	{
		if (!Capture(InContext, Path, InSide, bRequired)) { return false; }

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
		if (!Capture(InContext, InSelector, InSide, bRequired)) { return false; }

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

	void IBufferProxy::SetSubSelection(const FSubSelection& InSubSelection)
	{
		bWantsSubSelection = InSubSelection.bIsValid;
		if (bWantsSubSelection) { CachedSubSelection.Initialize(InSubSelection, RealType, WorkingType); }
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
		if (WorkingOps) { return WorkingOps->GetTypeSize(); }
		return PCGExTypes::FScopedTypedValue::GetTypeSize(WorkingType);
	}

	int32 IBufferProxy::GetValueAlignment() const
	{
		if (WorkingOps) { return WorkingOps->GetTypeAlignment(); }
		return 1;
	}

#pragma endregion

	TSharedPtr<IBufferProxy> IBufferProxyPool::TryGet(const FProxyDescriptor& Descriptor)
	{
		FReadScopeLock ReadScopeLock(MapLock);
		TWeakPtr<IBufferProxy>* ProxyPtr = ProxyMap.Find(GetTypeHash(Descriptor));
		if (!ProxyPtr) { return nullptr; }
		return ProxyPtr->Pin();
	}

	void IBufferProxyPool::Add(const FProxyDescriptor& Descriptor, const TSharedPtr<IBufferProxy>& Proxy)
	{
		// Will replace existing, it's fine, we can live with a few duplicate proxies
		FWriteScopeLock WriteScopeLock(MapLock);
		ProxyMap.Add(GetTypeHash(Descriptor), Proxy);
	}
}
