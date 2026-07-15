// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExDataHelpers.h"

#include "PCGExLog.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExSubSelection.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Types/PCGExTypes.h"

namespace PCGExData::Helpers
{
	void CopyBuffersValues(
		const TSharedPtr<FFacade>& SourceFacade,
		const TSharedPtr<FFacade>& TargetFacade,
		const TArray<int32>& SourcePointIndices,
		const TSet<FName>* IgnoreList)
	{
		for (const TSharedPtr<IBuffer>& SrcBuffer : SourceFacade->Buffers)
		{
			if (!SrcBuffer || !SrcBuffer->IsWritable() || !SrcBuffer->IsEnabled() ||
				(IgnoreList && IgnoreList->Contains(SrcBuffer->Identifier.Name)))
			{
				continue;
			}

			EPCGMetadataTypes SrcType = SrcBuffer->GetTypeId();
			TSharedPtr<IBuffer> DstBuffer = TargetFacade->GetWritable(SrcType, SrcBuffer->Identifier.Name, EBufferInit::Inherit);
			if (!DstBuffer)
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(SrcType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				if (SrcBuffer->GetUnderlyingDomain() == EDomainType::Elements)
				{
					TArray<T>& SrcValues = *StaticCastSharedPtr<TArrayBuffer<T>>(SrcBuffer)->GetOutValues().Get();
					TArray<T>& DstValues = *StaticCastSharedPtr<TArrayBuffer<T>>(DstBuffer)->GetOutValues().Get();

					for (int32 i = 0; i < SourcePointIndices.Num(); i++)
					{
						DstValues[i] = SrcValues[SourcePointIndices[i]];
					}
				}
				else
				{
					// TODO 
				}
			});
		}
	}

	PCGMetadataEntryKey GetDataValueKey(const FPCGMetadataAttributeBase* Attribute)
	{
		if (!Attribute)
		{
			return PCGDefaultValueKey;
		}

		const FPCGMetadataDomain* Domain = Attribute->GetMetadataDomain();
		return (Domain && Domain->GetItemCountForChild() > 0) ? PCGFirstEntryKey : PCGDefaultValueKey;
	}

	bool HasPropertyCopyableValue(const FPCGMetadataAttributeBase* Attribute, const PCGMetadataEntryKey /*Key*/)
	{
		// 5.7 metadata attributes always resolve a value at their data key (default slot or first entry
		// via GetDataValueKey), so readability reduces to the attribute existing. 5.8's void read-address
		// probe existed to reject extended/container attributes with no stored value; those attribute
		// types don't exist on 5.7, so there is nothing extra to guard against here.
		return Attribute != nullptr;
	}

	bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, const PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, const PCGMetadataEntryKey TargetKey)
	{
		return PropertyCopyAttribute(SourceAttr, SourceKey, TargetAttr, MakeArrayView(&TargetKey, 1));
	}

	bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, const PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, const TArrayView<const PCGMetadataEntryKey> TargetKeys)
	{
		if (!SourceAttr || !TargetAttr || TargetKeys.IsEmpty())
		{
			return false;
		}

		// 5.7 has no type-erased property setter (5.8's SetValueFromProperty / GetReadAddressFromEntryKey_Unsafe),
		// so recover the concrete type and copy typed. Every 5.7 metadata attribute is one of the supported
		// scalar/vector types, so ExecuteWithRightType covers them all. Types must match.
		if (SourceAttr->GetTypeId() != TargetAttr->GetTypeId())
		{
			return false;
		}

		bool bCopied = false;
		PCGExMetaHelpers::ExecuteWithRightType(
			SourceAttr->GetTypeId(),
			[&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				const FPCGMetadataAttribute<T>* TypedSource = static_cast<const FPCGMetadataAttribute<T>*>(SourceAttr);
				FPCGMetadataAttribute<T>* TypedTarget = static_cast<FPCGMetadataAttribute<T>*>(TargetAttr);
				const T Value = TypedSource->GetValueFromItemKey(SourceKey);
				for (const PCGMetadataEntryKey TargetKey : TargetKeys)
				{
					if (TargetKey == PCGDefaultValueKey)
					{
						TypedTarget->SetDefaultValue(Value);
					}
					else
					{
						TypedTarget->SetValue(TargetKey, Value);
					}
				}
				bCopied = true;
			});

		return bCopied;
	}

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttribute<T>* Attribute)
	{
		// Read a single value from a @Data domain attribute (one value per dataset, not per-point).
		// The default-value slot is the canonical @Data store, mirroring the engine: accessor keys
		// resolve to the default slot when the data domain has no items (bAddDefaultValueIfEmpty in
		// the accessor factory), and attribute copies across node boundaries never carry entries
		// (CopyInternal: bCopyEntries=false) -- only the default, which is copied eagerly at copy
		// time and is therefore also GC-proof. Entry reads apply only when the domain actually has
		// items, exactly like engine accessors; GetValueFromItemKey then resolves inherited entries
		// through the attribute parent chain the same way the engine would.

		if (!ensure(Attribute))
		{
			// Should not happen, callsite need to gate against reading nothing
			return T();
		}

		const FPCGMetadataDomain* Domain = Attribute->GetMetadataDomain();
		if (Domain && Domain->GetItemCountForChild() > 0)
		{
			return Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		}

		return Attribute->GetValue(PCGDefaultValueKey);
	}

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute, T Fallback)
	{
		T Value = Fallback;
		PCGExMetaHelpers::ExecuteWithRightType(Attribute->GetTypeId(), [&](auto DummyValue)
		{
			using T_VALUE = decltype(DummyValue);
			Value = PCGExTypeOps::Convert<T_VALUE, T>(ReadDataValue<T_VALUE>(static_cast<const FPCGMetadataAttribute<T_VALUE>*>(Attribute)));
		});
		return Value;
	}

	template <typename T>
	void SetDataValue(FPCGMetadataAttribute<T>* Attribute, const T Value)
	{
		Attribute->SetDefaultValue(Value);
		
		const FPCGMetadataDomain* Domain = Attribute->GetMetadataDomain();
		if (Domain && Domain->GetItemCountForChild() > 0)
		{
			Attribute->SetValue(PCGFirstEntryKey, Value);
		}
	}

	template <typename T>
	void SetDataValue(UPCGData* InData, FName Name, const T Value)
	{
		FPCGAttributePropertyInputSelector SafetySelector;
		SafetySelector.Update(Name.ToString());

		if (SafetySelector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			UE_LOG(LogPCGEx, Error, TEXT("Attempting to write @Data value to a non-attribute domain."))
			return;
		}

		FPCGAttributeIdentifier Identifier = FPCGAttributeIdentifier(SafetySelector.GetAttributeName(), EPCGMetadataDomainFlag::Data);
		SetDataValue<T>(InData->Metadata->FindOrCreateAttribute<T>(Identifier, Value, true, true), Value);
	}

	template <typename T>
	void SetDataValue(UPCGData* InData, FPCGAttributeIdentifier Identifier, const T Value)
	{
		SetDataValue<T>(InData, Identifier.Name, Value);
	}

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttribute<_TYPE>* Attribute); \
template PCGEXCORE_API _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute, _TYPE Fallback); \
template PCGEXCORE_API void SetDataValue<_TYPE>(FPCGMetadataAttribute<_TYPE>* Attribute, const _TYPE Value); \
template PCGEXCORE_API void SetDataValue<_TYPE>(UPCGData* InData, FName Name, const _TYPE Value); \
template PCGEXCORE_API void SetDataValue<_TYPE>(UPCGData* InData, FPCGAttributeIdentifier Identifier, const _TYPE Value);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	template <typename T>
	bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, T& OutValue, const bool bQuiet)
	{
		bool bSuccess = false;
		const UPCGMetadata* InMetadata = InData->Metadata;

		if (!InMetadata)
		{
			return false;
		}

		FSubSelection SubSelection(InSelector);
		FPCGAttributeIdentifier SanitizedIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(InSelector, InData);
		SanitizedIdentifier.MetadataDomain = EPCGMetadataDomainFlag::Data; // Force data domain

		// Domain-safe lookup: the data may have never instantiated its @Data domain.
		if (const FPCGMetadataAttributeBase* SourceAttribute = PCGExMetaHelpers::TryGetConstAttribute(InMetadata, SanitizedIdentifier))
		{
			PCGExMetaHelpers::ExecuteWithRightType(SourceAttribute->GetTypeId(), [&](auto DummyValue)
			{
				using T_VALUE = decltype(DummyValue);

				const FPCGMetadataAttribute<T_VALUE>* TypedSource = static_cast<const FPCGMetadataAttribute<T_VALUE>*>(SourceAttribute);
				const T_VALUE Value = ReadDataValue(TypedSource);

				if (SubSelection.bIsValid)
				{
					OutValue = SubSelection.Get<T_VALUE, T>(Value);
				}
				else
				{
					OutValue = PCGExTypeOps::Convert<T_VALUE, T>(Value);
				}

				bSuccess = true;
			});
		}
		else
		{
			if (!bQuiet && InContext)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Attribute, InSelector)
			}
			return false;
		}

		return bSuccess;
	}

	template <typename T>
	bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, const FName& InName, T& OutValue, const bool bQuiet)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InName.ToString());
		return TryReadDataValue<T>(InContext, InData, Selector.CopyAndFixLast(InData), OutValue, bQuiet);
	}

	template <typename T>
	bool TryReadDataValue(const TSharedPtr<FPointIO>& InIO, const FName& InName, T& OutValue, const bool bQuiet)
	{
		return TryReadDataValue(InIO->GetContext(), InIO->GetIn(), InName, OutValue, bQuiet);
	}

	template <typename T>
	bool TryReadDataValue(const TSharedPtr<FPointIO>& InIO, const FPCGAttributePropertyInputSelector& InSelector, T& OutValue, const bool bQuiet)
	{
		return TryReadDataValue(InIO->GetContext(), InIO->GetIn(), InSelector, OutValue, bQuiet);
	}

	template <typename T>
	bool TryGetSettingDataValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const T& InConstant, T& OutValue, const bool bQuiet)
	{
		if (Input == EPCGExInputValueType::Constant)
		{
			OutValue = InConstant;
			return true;
		}

		return TryReadDataValue<T>(InContext, InData, InSelector, OutValue, bQuiet);
	}

	template <typename T>
	bool TryGetSettingDataValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FName& InName, const T& InConstant, T& OutValue, const bool bQuiet)
	{
		if (Input == EPCGExInputValueType::Constant)
		{
			OutValue = InConstant;
			return true;
		}

		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InName.ToString());
		return TryReadDataValue<T>(InContext, InData, Selector.CopyAndFixLast(InData), OutValue, bQuiet);
	}

	template <typename T>
	bool TryGetSettingDataValue(const TSharedPtr<FPointIO>& InIO, const EPCGExInputValueType Input, const FName& InName, const T& InConstant, T& OutValue, const bool bQuiet)
	{
		return TryGetSettingDataValue(InIO->GetContext(), InIO->GetIn(), Input, InName, InConstant, OutValue, bQuiet);
	}

	template <typename T>
	bool TryGetSettingDataValue(const TSharedPtr<FPointIO>& InIO, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const T& InConstant, T& OutValue, const bool bQuiet)
	{
		return TryGetSettingDataValue(InIO->GetContext(), InIO->GetIn(), Input, InSelector, InConstant, OutValue, bQuiet);
	}

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API bool TryReadDataValue<_TYPE>(FPCGExContext* InContext, const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryReadDataValue<_TYPE>(FPCGExContext* InContext, const UPCGData* InData, const FName& InName, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryReadDataValue<_TYPE>(const TSharedPtr<PCGExData::FPointIO>& InIO, const FName& InName, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryReadDataValue<_TYPE>(const TSharedPtr<PCGExData::FPointIO>& InIO, const FPCGAttributePropertyInputSelector& InSelector, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryGetSettingDataValue<_TYPE>( FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryGetSettingDataValue<_TYPE>( FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FName& InName, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryGetSettingDataValue<_TYPE>( const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExInputValueType Input, const FName& InName, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
template PCGEXCORE_API bool TryGetSettingDataValue<_TYPE>( const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL
}
