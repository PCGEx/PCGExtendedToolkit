// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExDataHelpers.h"

#include "PCGExLog.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExSubSelection.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Types/PCGExAttributeIdentity.h"
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

			// TODO: support Data domain. Currently only Elements.
			if (SrcBuffer->GetUnderlyingDomain() != EDomainType::Elements)
			{
				continue;
			}

			const FPCGMetadataAttributeBase* SrcAttr = SrcBuffer->OutAttribute;
			if (!SrcAttr)
			{
				continue;
			}

			// Attribute-driven: tier-agnostic, no EPCGMetadataTypes surfaces here.
			TSharedPtr<IBuffer> DstBuffer = TargetFacade->GetWritableFromAttribute(SrcAttr, EBufferInit::Inherit);
			if (!DstBuffer)
			{
				continue;
			}

			const int32 NumCopy = SourcePointIndices.Num();

			// Scope-based parallel: one FScopedTypedValue per worker task (not per iteration).
			// Amortizes the scoped-value construction cost across each thread's chunk.
			PCGExMT::ParallelOrSequentialScoped(
				NumCopy,
				[&](const PCGExMT::FScope& Scope)
				{
					PCGExTypes::FScopedTypedValue Temp = SrcBuffer->MakeScopedValue();
					PCGEX_SCOPE_LOOP(i)
					{
						SrcBuffer->GetVoid(SourcePointIndices[i], Temp);
						DstBuffer->SetVoid(i, Temp);
					}
				},
				1024);
		}
	}

	bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, const PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, const PCGMetadataEntryKey TargetKey)
	{
		if (!SourceAttr || !TargetAttr)
		{
			return false;
		}

		const void* SrcAddr = SourceAttr->GetReadAddressFromEntryKey_Unsafe(SourceKey);
		if (!SrcAddr)
		{
			return false;
		}

		// PERF -- this allocates a transient FProperty per call (CreateInnerPropertyFromDesc walks the
		// desc + heap-allocates) and deletes it. Fine for one-shot single-value carry (DataForward,
		// PointsToBounds, BlendingHelpers per-attribute outer loop). NOT fine for per-element loops --
		// if you need that, drive the loop through an FPropertyBuffer instance whose CachedInnerProperty
		// is built once at InitForRead/InitForWrite, and call SetFromVoidProperty per element.
		FProperty* TempProp = FPropertyBuffer::CreateInnerPropertyFromDesc(SourceAttr->GetAttributeDesc());
		if (!TempProp)
		{
			return false;
		}

		TargetAttr->SetValueFromProperty(TargetKey, SrcAddr, TempProp);
		delete TempProp;
		return true;
	}

	bool PropertyBroadcastAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, const PCGMetadataEntryKey SourceKey,
		const TSharedPtr<IBuffer>& TargetWriter)
	{
		if (!SourceAttr || !TargetWriter)
		{
			return false;
		}

		const void* SrcAddr = SourceAttr->GetReadAddressFromEntryKey_Unsafe(SourceKey);
		if (!SrcAddr)
		{
			return false;
		}

		PCGExTypes::FScopedTypedValue Scratch = TargetWriter->MakeScopedValue();
		const FProperty* Prop = Scratch.GetProperty();
		if (!Prop)
		{
			return false;
		}

		Prop->CopyCompleteValue(Scratch.GetRaw(), SrcAddr);
		const int32 NumSlots = TargetWriter->GetNumValues(EIOSide::Out);
		for (int32 s = 0; s < NumSlots; s++)
		{
			TargetWriter->SetVoid(s, Scratch);
		}
		return NumSlots > 0;
	}

	bool PropertyScatterAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, const PCGMetadataEntryKey SourceKey,
		const TSharedPtr<IBuffer>& TargetWriter, TArrayView<const int32> Indices)
	{
		if (!SourceAttr || !TargetWriter || Indices.IsEmpty())
		{
			return false;
		}

		const void* SrcAddr = SourceAttr->GetReadAddressFromEntryKey_Unsafe(SourceKey);
		if (!SrcAddr)
		{
			return false;
		}

		PCGExTypes::FScopedTypedValue Scratch = TargetWriter->MakeScopedValue();
		const FProperty* Prop = Scratch.GetProperty();
		if (!Prop)
		{
			return false;
		}

		Prop->CopyCompleteValue(Scratch.GetRaw(), SrcAddr);
		for (int32 Index : Indices)
		{
			TargetWriter->SetVoid(Index, Scratch);
		}
		return true;
	}

	bool PropertyCopyAttributeRange(
		const TSharedPtr<FPointIO>& SourceIO, const FAttributeIdentity& SourceIdentity,
		const TSharedRef<FPropertyArrayBuffer>& TargetBuffer,
		const PCGExMT::FScope& ReadScope, const PCGExMT::FScope& WriteScope, const bool bReverse)
	{
		check(ReadScope.Count == WriteScope.Count);

		if (!SourceIO || ReadScope.Count <= 0)
		{
			return false;
		}

		const UPCGBasePointData* SourceData = SourceIO->GetIn();
		if (!SourceData || !SourceData->Metadata)
		{
			return false;
		}

		const FPCGMetadataAttributeBase* SourceAttr = SourceData->Metadata->GetConstAttribute(SourceIdentity.GetIdentifier());
		if (!SourceAttr)
		{
			return false;
		}

		auto EntryKeys = SourceData->GetConstMetadataEntryValueRange();
		if (EntryKeys.Num() < ReadScope.End)
		{
			return false;
		}

		bool bAnyCopied = false;
		for (int32 i = 0; i < ReadScope.Count; i++)
		{
			const int32 ReadIdx = bReverse ? (ReadScope.End - 1 - i) : (ReadScope.Start + i);
			const PCGMetadataEntryKey EntryKey = EntryKeys[ReadIdx];
			const void* SrcAddr = SourceAttr->GetReadAddressFromEntryKey_Unsafe(EntryKey);
			if (!SrcAddr)
			{
				continue;
			}
			TargetBuffer->SetFromVoidProperty(WriteScope.Start + i, SrcAddr);
			bAnyCopied = true;
		}
		return bAnyCopied;
	}

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute)
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
			return Attribute->GetValueFromItemKey<T>(PCGFirstEntryKey);
		}

		// PCGDefaultValueKey == PCGInvalidEntryKey routes GetValueFromItemKey to the default slot.
		return Attribute->GetValueFromItemKey<T>(PCGDefaultValueKey);
	}

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute, T Fallback)
	{
		T Value = Fallback;
		// Container/extended types fall through (no meaningful conversion to templated T) -- fallback wins.
		PCGExMetaHelpers::ExecuteWithRightType(Attribute, [&](auto DummyValue)
		{
			using T_VALUE = decltype(DummyValue);
			Value = PCGExTypeOps::Convert<T_VALUE, T>(ReadDataValue<T_VALUE>(static_cast<const FPCGMetadataAttribute<T_VALUE>*>(Attribute)));
		});
		return Value;
	}

	template <typename T>
	void SetDataValue(FPCGMetadataAttributeBase* Attribute, const T Value)
	{
		// The default-value slot is the canonical @Data store (see ReadDataValue). Do NOT materialize
		// an entry here: entries are invisible to the engine while the data domain has no items, but
		// they survive full data copies -- a later engine-side rewrite of the attribute (which goes to
		// the default slot) would leave the copied entry stale, and item-keyed reads prefer entries.
		// CRITICAL -- this must be SetDefaultValue, not SetValue<T>(PCGDefaultValueKey, ...):
		// PCGDefaultValueKey == -1 == PCGInvalidEntryKey and the engine's SetValueFromValueKey_Unsafe
		// silently drops writes for invalid entry keys.
		Attribute->SetDefaultValue(Value);
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
template PCGEXCORE_API _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute); \
template PCGEXCORE_API _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute, _TYPE Fallback); \
template PCGEXCORE_API void SetDataValue<_TYPE>(FPCGMetadataAttributeBase* Attribute, const _TYPE Value); \
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
			// Container/extended source types: TryReadDataValue<T> can't represent them; falls through to bSuccess=false.
			PCGExMetaHelpers::ExecuteWithRightType(SourceAttribute, [&](auto DummyValue)
			{
				using T_VALUE = decltype(DummyValue);

				const T_VALUE Value = ReadDataValue<T_VALUE>(SourceAttribute);

				if (SubSelection.HasSelection())
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
