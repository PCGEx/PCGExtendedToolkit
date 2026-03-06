// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExAttributeBroadcaster.h"

#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Data/PCGExDataHelpers.h"
#include "PCGParamData.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Types/PCGExTypeOpsImpl.h"
#include "Data/PCGExTaggedData.h"

namespace PCGExData
{
#pragma region Attribute utils

	IAttributeBroadcaster::FAttributeProcessingInfos::FAttributeProcessingInfos(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector)
	{
		Init(InData, InSelector);
	}

	IAttributeBroadcaster::FAttributeProcessingInfos::FAttributeProcessingInfos(const UPCGData* InData, const FName InAttributeName)
	{
		FPCGAttributePropertyInputSelector ProxySelector = FPCGAttributePropertyInputSelector();
		ProxySelector.Update(InAttributeName.ToString());
		Init(InData, ProxySelector);
	}

	IAttributeBroadcaster::FAttributeProcessingInfos::operator EPCGMetadataTypes() const
	{
		if (Attribute) { return static_cast<EPCGMetadataTypes>(Attribute->GetTypeId()); }
		return EPCGMetadataTypes::Unknown;
	}

	void IAttributeBroadcaster::FAttributeProcessingInfos::Init(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector)
	{
		Selector = InSelector.CopyAndFixLast(InData);
		bIsValid = Selector.IsValid();

		if (!bIsValid) { return; }

		SubSelection = FSubSelection(Selector.GetExtraNames());

		if (Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			Attribute = nullptr;
			bIsValid = false;

			if (const UPCGSpatialData* AsSpatial = Cast<UPCGSpatialData>(InData))
			{
				Attribute = AsSpatial->Metadata->GetConstAttribute(PCGExMetaHelpers::GetAttributeIdentifier(Selector, InData));
				bIsDataDomain = Attribute ? Attribute->GetMetadataDomain()->GetDomainID().Flag == EPCGMetadataDomainFlag::Data : false;
				bIsValid = Attribute ? true : false;
			}
		}
	}

#pragma endregion

#pragma region Attribute Broadcaster

	const FPCGMetadataAttributeBase* IAttributeBroadcaster::GetAttribute() const { return ProcessingInfos; }

	EPCGMetadataTypes IAttributeBroadcaster::GetMetadataType() const
	{
		return EPCGMetadataTypes::Unknown;
	}

	FName IAttributeBroadcaster::GetName() const { return NAME_None; }

	template <typename T>
	bool TAttributeBroadcaster<T>::ApplySelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData)
	{
		// Two distinct paths depending on attribute domain:
		// @Data domain: Read a single value (DataValue) and cache its typed conversion (TypedDataValue).
		//   All subsequent Fetch/FetchSingle calls return this constant for every point.
		// Element domain: Create a PCG accessor (InternalAccessor) for batch per-point reads via GetRange.
		static_assert(Traits::Type != EPCGMetadataTypes::Unknown, "T must be of PCG-friendly type. Custom types are unsupported -- you'll have to static_cast the values.");

		ProcessingInfos = FAttributeProcessingInfos(InData, InSelector);
		if (!ProcessingInfos.bIsValid) { return false; }

		if (ProcessingInfos.bIsDataDomain)
		{
			PCGExMetaHelpers::ExecuteWithRightType(ProcessingInfos.Attribute->GetTypeId(), [&](auto DummyValue)
			{
				using T_REAL = decltype(DummyValue);
				DataValue = MakeShared<TDataValue<T_REAL>>(Helpers::ReadDataValue(static_cast<const FPCGMetadataAttribute<T_REAL>*>(ProcessingInfos.Attribute)));

				const FSubSelection& S = ProcessingInfos.SubSelection;
				TypedDataValue = S.bIsValid ? S.Get<T_REAL, T>(DataValue->GetValue<T_REAL>()) : PCGExTypeOps::Convert<T_REAL, T>(DataValue->GetValue<T_REAL>());
			});
		}
		else
		{
			InternalAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, ProcessingInfos.Selector);
			ProcessingInfos.bIsValid = InternalAccessor.IsValid();
		}

		return ProcessingInfos.bIsValid;
	}

	template <typename T>
	FName TAttributeBroadcaster<T>::GetName() const { return ProcessingInfos.Selector.GetName(); }

	template <typename T>
	bool TAttributeBroadcaster<T>::IsUsable(int32 NumEntries) { return ProcessingInfos.bIsValid && Values.Num() >= NumEntries; }

	template <typename T>
	bool TAttributeBroadcaster<T>::Prepare(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO)
	{
		Keys = InPointIO->GetInKeys();
		Min = Traits::Min();
		Max = Traits::Max();
		return ApplySelector(InSelector, InPointIO->GetIn());
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::Prepare(const FName& InName, const TSharedRef<FPointIO>& InPointIO)
	{
		FPCGAttributePropertyInputSelector InSelector = FPCGAttributePropertyInputSelector();
		InSelector.Update(InName.ToString());
		return Prepare(InSelector, InPointIO);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::Prepare(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO)
	{
		return Prepare(PCGExMetaHelpers::GetSelectorFromIdentifier(InIdentifier), InPointIO);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys)
	{
		if (InKeys) { Keys = InKeys; }
		else if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData)) { Keys = MakeShared<FPCGAttributeAccessorKeysPointIndices>(PointData); }
		else if (InData->Metadata) { Keys = MakeShared<FPCGAttributeAccessorKeysEntries>(InData->Metadata); }

		if (!Keys) { return false; }

		Min = Traits::Min();
		Max = Traits::Max();
		return ApplySelector(InSelector, InData);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FName& InName, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys)
	{
		FPCGAttributePropertyInputSelector InSelector = FPCGAttributePropertyInputSelector();
		InSelector.Update(InName.ToString());
		return PrepareForSingleFetch(InSelector, InData, InKeys);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FPCGAttributeIdentifier& InIdentifier, const UPCGData* InData, const TSharedPtr<IPCGAttributeAccessorKeys> InKeys)
	{
		return PrepareForSingleFetch(PCGExMetaHelpers::GetSelectorFromIdentifier(InIdentifier), InData, InKeys);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FPCGAttributePropertyInputSelector& InSelector, const FPCGExTaggedData& InData)
	{
		return PrepareForSingleFetch(InSelector, InData.Data, InData.Keys);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FName& InName, const FPCGExTaggedData& InData)
	{
		return PrepareForSingleFetch(InName, InData.Data, InData.Keys);
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::PrepareForSingleFetch(const FPCGAttributeIdentifier& InIdentifier, const FPCGExTaggedData& InData)
	{
		return PrepareForSingleFetch(InIdentifier, InData.Data, InData.Keys);
	}

	template <typename T>
	void TAttributeBroadcaster<T>::Fetch(TArray<T>& Dump, const PCGExMT::FScope& Scope)
	{
		check(ProcessingInfos.bIsValid)
		check(Dump.Num() == Keys->GetNum()) // Dump target should be initialized at full length before using Fetch

		if (!ProcessingInfos.bIsValid)
		{
			PCGEX_SCOPE_LOOP(i) { Dump[i] = T{}; }
			return;
		}

		if (DataValue)
		{
			PCGEX_SCOPE_LOOP(i) { Dump[i] = TypedDataValue; }
		}
		else
		{
			TArrayView<T> DumpView = MakeArrayView(Dump.GetData() + Scope.Start, Scope.Count);
			const bool bSuccess = InternalAccessor->GetRange<T>(DumpView, Scope.Start, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);

			if (!bSuccess)
			{
				// TODO : Log error
			}
		}
	}

	template <typename T>
	void TAttributeBroadcaster<T>::GrabAndDump(TArray<T>& Dump, const bool bCaptureMinMax, T& OutMin, T& OutMax)
	{
		const int32 NumPoints = Keys->GetNum();
		PCGExArrayHelpers::InitArray(Dump, NumPoints);

		OutMin = Traits::Max();
		OutMax = Traits::Min();

		if (!ProcessingInfos.bIsValid)
		{
			for (int i = 0; i < NumPoints; i++) { Dump[i] = T{}; }
			return;
		}

		if (DataValue)
		{
			for (int i = 0; i < NumPoints; i++) { Dump[i] = TypedDataValue; }

			if (bCaptureMinMax)
			{
				OutMin = TypedDataValue;
				OutMax = TypedDataValue;
			}
		}
		else
		{
			const bool bSuccess = InternalAccessor->GetRange<T>(Dump, 0, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
			if (!bSuccess)
			{
				// TODO : Log error
			}
			else if (bCaptureMinMax)
			{
				for (int i = 0; i < NumPoints; i++)
				{
					const T& V = Dump[i];
					OutMin = PCGExTypeOps::FTypeOps<T>::Min(V, OutMin);
					OutMax = PCGExTypeOps::FTypeOps<T>::Max(V, OutMax);
				}
			}
		}
	}

	template <typename T>
	void TAttributeBroadcaster<T>::GrabUniqueValues(TSet<T>& OutUniqueValues)
	{
		if (!ProcessingInfos.bIsValid) { return; }

		// TODO : Revise this work with a custom has function and output an array of unique values instead

		if constexpr (std::is_same_v<T, FRotator> || std::is_same_v<T, FTransform> || std::is_same_v<T, FText>)
		{
			UE_LOG(LogTemp, Error, TEXT("Unique value type is unsupported at the moment."))
		}
		else
		{
			if (DataValue)
			{
				OutUniqueValues.Add(TypedDataValue);
			}
			else
			{
				T TempMin = T{};
				T TempMax = T{};

				int32 NumPoints = Keys->GetNum();
				OutUniqueValues.Reserve(OutUniqueValues.Num() + NumPoints);

				TArray<T> Dump;
				GrabAndDump(Dump, false, TempMin, TempMax);
				OutUniqueValues.Append(Dump);

				OutUniqueValues.Shrink();
			}
		}
	}

	template <typename T>
	void TAttributeBroadcaster<T>::Grab(const bool bCaptureMinMax)
	{
		GrabAndDump(Values, bCaptureMinMax, Min, Max);
	}

	template <typename T>
	T TAttributeBroadcaster<T>::FetchSingle(const FElement& Element, const T& Fallback) const
	{
		if (!ProcessingInfos.bIsValid) { return Fallback; }
		if (DataValue) { return TypedDataValue; }

		T OutValue = Fallback;
		if (!InternalAccessor->Get<T>(OutValue, Element.Index, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible)) { OutValue = Fallback; }
		return OutValue;
	}

	template <typename T>
	bool TAttributeBroadcaster<T>::TryFetchSingle(const FElement& Element, T& OutValue) const
	{
		if (!ProcessingInfos.bIsValid) { return false; }
		if (DataValue)
		{
			OutValue = TypedDataValue;
			return true;
		}

		return InternalAccessor->Get<T>(OutValue, Element.Index, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}

	template <typename T>
	EPCGMetadataTypes TAttributeBroadcaster<T>::GetMetadataType() const
	{
		return Traits::Type;
	}

	TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FName& InName, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		return MakeBroadcaster(FPCGAttributeIdentifier(InName), InPointIO, bSingleFetch);
	}

	TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		const FPCGMetadataAttributeBase* Attribute = InPointIO->FindConstAttribute(InIdentifier);
		if (!Attribute) { return nullptr; }

		TSharedPtr<IAttributeBroadcaster> Broadcaster = nullptr;
		PCGExMetaHelpers::ExecuteWithRightType(Attribute->GetTypeId(), [&](auto DummyValue)
		{
			using T = decltype(DummyValue);
			TSharedPtr<TAttributeBroadcaster<T>> TypedBroadcaster = MakeShared<TAttributeBroadcaster<T>>();
			if (!(bSingleFetch ? TypedBroadcaster->PrepareForSingleFetch(InIdentifier, InPointIO->GetIn()) : TypedBroadcaster->Prepare(InIdentifier, InPointIO))) { return; }
			Broadcaster = TypedBroadcaster;
		});

		return Broadcaster;
	}

	TSharedPtr<IAttributeBroadcaster> MakeBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		const UPCGData* InData = InPointIO->GetIn();
		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
		if (!TryGetType(InSelector, InData, Type)) { return nullptr; }

		TSharedPtr<IAttributeBroadcaster> Broadcaster = nullptr;
		PCGExMetaHelpers::ExecuteWithRightType(Type, [&](auto DummyValue)
		{
			using T = decltype(DummyValue);
			TSharedPtr<TAttributeBroadcaster<T>> TypedBroadcaster = MakeShared<TAttributeBroadcaster<T>>();
			if (!(bSingleFetch ? TypedBroadcaster->PrepareForSingleFetch(InSelector, InData) : TypedBroadcaster->Prepare(InSelector, InPointIO))) { return; }
			Broadcaster = TypedBroadcaster;
		});

		return Broadcaster;
	}

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FName& InName, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		PCGEX_MAKE_SHARED(Broadcaster, TAttributeBroadcaster<T>)
		if (!(bSingleFetch ? Broadcaster->PrepareForSingleFetch(InName, InPointIO->GetIn()) : Broadcaster->Prepare(InName, InPointIO))) { return nullptr; }
		return Broadcaster;
	}

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InIdentifier.ToString());
		return MakeTypedBroadcaster<T>(Selector, InPointIO, bSingleFetch);
	}

	template <typename T>
	TSharedPtr<TAttributeBroadcaster<T>> MakeTypedBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<FPointIO>& InPointIO, bool bSingleFetch)
	{
		PCGEX_MAKE_SHARED(Broadcaster, TAttributeBroadcaster<T>)
		if (!(bSingleFetch ? Broadcaster->PrepareForSingleFetch(InSelector, InPointIO->GetIn()) : Broadcaster->Prepare(InSelector, InPointIO))) { return nullptr; }
		return Broadcaster;
	}

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TAttributeBroadcaster<_TYPE>;\
template PCGEXCORE_API TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FName& InName, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch); \
template PCGEXCORE_API TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FPCGAttributeIdentifier& InIdentifier, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch); \
template PCGEXCORE_API TSharedPtr<TAttributeBroadcaster<_TYPE>> MakeTypedBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const TSharedRef<PCGExData::FPointIO>& InPointIO, bool bSingleFetch);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion
}
