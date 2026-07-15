// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExSettingsDetails.h"

#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExTypes.h"

namespace PCGExDetails
{
	template <typename T>
	bool TSettingValue<T>::Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		if (!InitInternal(InDataFacade, bSupportScoped, bCaptureMinMax))
		{
			return false;
		}

		// Single registration site for the whole hierarchy: whatever attribute this value consumes
		// is registered here, after a successful init, and only when the node can act on it.
		// A null facade is a legal input -- constant values are routinely initialized without one
		FPCGExContext* Context = (bRegisterConsumable && InDataFacade) ? InDataFacade->GetContext() : nullptr;
		if (Context && Context->bCleanupConsumableAttributes)
		{
			const FName ConsumableName = GetConsumableName(InDataFacade->GetIn());
			if (!ConsumableName.IsNone())
			{
				Context->AddConsumableAttributeName(ConsumableName);
			}
		}

		return true;
	}

	template <typename T>
	bool TSettingValueBuffer<T>::InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		FPCGExContext* Context = InDataFacade->GetContext();
		if (!Context)
		{
			return false;
		}

		PCGEX_VALIDATE_NAME_C(Context, Name)

		Buffer = InDataFacade->GetBroadcaster<T>(Name, bSupportScoped, bCaptureMinMax);
		//Buffer = InDataFacade->GetReadable<T>(Name, PCGExData::EIOSide::In, bSupportScoped);

		if (!Buffer)
		{
			if (!this->bQuiet)
			{
				PCGEX_LOG_INVALID_ATTR_C(Context, Attribute, Name)
			}
			return false;
		}

		return true;
	}

	template <typename T>
	T TSettingValueBuffer<T>::Read(const int32 Index)
	{
		return Buffer->Read(Index);
	}

	template <typename T>
	void TSettingValueBuffer<T>::ReadScope(const int32 Start, TArrayView<T> OutResults)
	{
		Buffer->Read(Start, OutResults);
	}

	template <typename T>
	T TSettingValueBuffer<T>::Min()
	{
		return Buffer->Min;
	}

	template <typename T>
	T TSettingValueBuffer<T>::Max()
	{
		return Buffer->Max;
	}

	template <typename T>
	uint32 TSettingValueBuffer<T>::ReadValueHash(const int32 Index)
	{
		return Buffer->ReadValueHash(Index);
	}

	template <typename T>
	bool TSettingValueSelector<T>::InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		FPCGExContext* Context = InDataFacade->GetContext();
		if (!Context)
		{
			return false;
		}

		Buffer = InDataFacade->GetBroadcaster<T>(Selector, bSupportScoped && !bCaptureMinMax, bCaptureMinMax, this->bQuiet);
		return Buffer != nullptr;
	}

	template <typename T>
	FName TSettingValueSelector<T>::GetConsumableName(const UPCGData* InData) const
	{
		// Domain-qualified so cleanup targets the right domain; NAME_None for non-attribute selections.
		return PCGExMetaHelpers::GetDomainQualifiedName(Selector, InData);
	}

	template <typename T>
	bool TSettingValueConstant<T>::InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		return true;
	}

	template <typename T>
	void TSettingValueConstant<T>::ReadScope(const int32 Start, TArrayView<T> OutResults)
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++)
		{
			OutResults[i] = Constant;
		}
	}

	template <typename T>
	uint32 TSettingValueConstant<T>::ReadValueHash(const int32 Index)
	{
		return PCGExTypes::ComputeHash(Constant);
	}

	template <typename T>
	bool TSettingValueSelectorConstant<T>::InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		FPCGExContext* Context = InDataFacade->GetContext();
		if (!Context)
		{
			return false;
		}

		return PCGExData::Helpers::TryReadDataValue(Context, InDataFacade->GetIn(), Selector, this->Constant, this->bQuiet);
	}

	template <typename T>
	FName TSettingValueSelectorConstant<T>::GetConsumableName(const UPCGData* InData) const
	{
		// This path reads @Data selectors: the registration must be domain-qualified or cleanup
		// would target the default domain instead.
		return PCGExMetaHelpers::GetDomainQualifiedName(Selector, InData);
	}

	template <typename T>
	bool TSettingValueBufferConstant<T>::InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax)
	{
		FPCGExContext* Context = InDataFacade->GetContext();
		if (!Context)
		{
			return false;
		}

		PCGEX_VALIDATE_NAME_C(Context, Name)

		return PCGExData::Helpers::TryReadDataValue(Context, InDataFacade->GetIn(), Name, this->Constant, this->bQuiet);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const T InConstant)
	{
		TSharedPtr<TSettingValueConstant<T>> V = MakeShared<TSettingValueConstant<T>>(InConstant);
		return StaticCastSharedPtr<TSettingValue<T>>(V);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant)
	{
		if (InInput == EPCGExInputValueType::Attribute)
		{
			if (PCGExMetaHelpers::IsDataDomainAttribute(InSelector))
			{
				TSharedPtr<TSettingValueSelectorConstant<T>> V = MakeShared<TSettingValueSelectorConstant<T>>(InSelector);
				return StaticCastSharedPtr<TSettingValue<T>>(V);
			}
			TSharedPtr<TSettingValueSelector<T>> V = MakeShared<TSettingValueSelector<T>>(InSelector);
			return StaticCastSharedPtr<TSettingValue<T>>(V);
		}

		return MakeSettingValue<T>(InConstant);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const EPCGExInputValueType InInput, const FName InName, const T InConstant)
	{
		if (InInput == EPCGExInputValueType::Attribute)
		{
			if (PCGExMetaHelpers::IsDataDomainAttribute(InName))
			{
				TSharedPtr<TSettingValueBufferConstant<T>> V = MakeShared<TSettingValueBufferConstant<T>>(InName);
				return StaticCastSharedPtr<TSettingValue<T>>(V);
			}
			TSharedPtr<TSettingValueBuffer<T>> V = MakeShared<TSettingValueBuffer<T>>(InName);
			return StaticCastSharedPtr<TSettingValue<T>>(V);
		}

		return MakeSettingValue<T>(InConstant);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FName InName, const T InConstant)
	{
		T Constant = InConstant;
		PCGExData::Helpers::TryGetSettingDataValue(InContext, InData, InInput, InName, InConstant, Constant);
		return MakeSettingValue<T>(Constant);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant)
	{
		T Constant = InConstant;
		PCGExData::Helpers::TryGetSettingDataValue(InContext, InData, InInput, InSelector, InConstant, Constant);
		return MakeSettingValue<T>(Constant);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FName InName, const T InConstant)
	{
		return MakeSettingValue<T>(InData->GetContext(), InData->GetIn(), InInput, InName, InConstant);
	}

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant)
	{
		return MakeSettingValue<T>(InData->GetContext(), InData->GetIn(), InInput, InSelector, InConstant);
	}

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TSettingValue<_TYPE>;\
template class PCGEXCORE_API TSettingValueBuffer<_TYPE>;\
template class PCGEXCORE_API TSettingValueSelector<_TYPE>;\
template class PCGEXCORE_API TSettingValueConstant<_TYPE>;\
template class PCGEXCORE_API TSettingValueSelectorConstant<_TYPE>;\
template class PCGEXCORE_API TSettingValueBufferConstant<_TYPE>;\
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
template PCGEXCORE_API TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion
}
