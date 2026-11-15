// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExAttributesToPins.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExPickerFactoryProvider.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExSubSelection.h"
#include "Factories/PCGExFactories.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

#define LOCTEXT_NAMESPACE "PCGExAttributesToPinsElement"
#define PCGEX_NAMESPACE AttributesToPins

#pragma region FPCGExAttributeToPinDetails

FName FPCGExAttributeToPinDetails::GetRemappedName() const
{
	return bOutputToDifferentName ? Target : NAME_None;
}

FName FPCGExAttributeToPinDetails::GetPinLabel() const
{
	if (Source.IsNone())
	{
		return NAME_None;
	}

	const FName RemappedName = GetRemappedName();
	return RemappedName.IsNone() ? GetSourceSelector().GetName() : RemappedName;
}

#pragma endregion

#pragma region PCGExAttributesToPins

namespace PCGExAttributesToPins
{
	EPCGMetadataTypes GetMetadataType(const EPCGExAttributeToPinType InType)
	{
		switch (InType)
		{
		case EPCGExAttributeToPinType::Auto:
			return EPCGMetadataTypes::Unknown;
		case EPCGExAttributeToPinType::Float:
			return EPCGMetadataTypes::Float;
		case EPCGExAttributeToPinType::Double:
			return EPCGMetadataTypes::Double;
		case EPCGExAttributeToPinType::Integer32:
			return EPCGMetadataTypes::Integer32;
		case EPCGExAttributeToPinType::Integer64:
			return EPCGMetadataTypes::Integer64;
		case EPCGExAttributeToPinType::Vector2:
			return EPCGMetadataTypes::Vector2;
		case EPCGExAttributeToPinType::Vector:
			return EPCGMetadataTypes::Vector;
		case EPCGExAttributeToPinType::Vector4:
			return EPCGMetadataTypes::Vector4;
		case EPCGExAttributeToPinType::Quaternion:
			return EPCGMetadataTypes::Quaternion;
		case EPCGExAttributeToPinType::Transform:
			return EPCGMetadataTypes::Transform;
		case EPCGExAttributeToPinType::String:
			return EPCGMetadataTypes::String;
		case EPCGExAttributeToPinType::Boolean:
			return EPCGMetadataTypes::Boolean;
		case EPCGExAttributeToPinType::Rotator:
			return EPCGMetadataTypes::Rotator;
		case EPCGExAttributeToPinType::Name:
			return EPCGMetadataTypes::Name;
		case EPCGExAttributeToPinType::SoftObjectPath:
			return EPCGMetadataTypes::SoftObjectPath;
		case EPCGExAttributeToPinType::SoftClassPath:
			return EPCGMetadataTypes::SoftClassPath;
		default:
			checkNoEntry();
			return EPCGMetadataTypes::Unknown;
		}
	}

	void GetRows(const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& PickerFactories, const int32 NumRows, TArray<int32>& OutRows)
	{
		OutRows.Reset();

		if (NumRows <= 0)
		{
			return;
		}

		if (PickerFactories.IsEmpty())
		{
			OutRows.Add(0);
			return;
		}

		PCGExPickers::GetSortedPicks(PickerFactories, NumRows, OutRows);
	}

	UPCGParamData* Extract(FPCGExContext* Context, const FPin& Pin, const UPCGData* Data, const TArray<int32>& Rows, const bool bDataDomainToElements)
	{
		const FPCGAttributePropertyInputSelector Selector = Pin.Selector.CopyAndFixLast(Data);
		const FText SourceText = FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(Selector));

		const FName OutputName = Pin.OutputName.IsNone() ? Selector.GetName() : Pin.OutputName;
		if (!PCGExMetaHelpers::IsWritableAttributeName(OutputName))
		{
			PCGEX_LOG_INVALID_INPUT(Context, FText::Format(LOCTEXT("InvalidOutputName", "'{0}' cannot be output as '{1}': not a valid attribute name."), SourceText, FText::FromName(OutputName)))
			return nullptr;
		}

		// Null for a property; a missing attribute fails the type lookup below.
		const FPCGMetadataAttributeBase* SourceAttribute = Selector.GetSelection() == EPCGAttributePropertySelection::Attribute
			                                                   ? PCGExMetaHelpers::TryGetConstAttribute(Data, PCGExMetaHelpers::GetAttributeIdentifier(Selector, Data))
			                                                   : nullptr;

		const bool bDataSource = SourceAttribute && SourceAttribute->GetMetadataDomain()->GetDomainID().Flag == EPCGMetadataDomainFlag::Data;
		const bool bDataTarget = bDataSource && !bDataDomainToElements;

		if (!bDataSource && Rows.IsEmpty())
		{
			PCGEX_LOG_INVALID_INPUT(Context, FText::Format(LOCTEXT("NoRow", "'{0}' has no row to read on one of the inputs."), SourceText))
			return nullptr;
		}

		UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
		UPCGMetadata* OutMetadata = OutputSet->Metadata;
		const FPCGAttributeIdentifier Identifier(OutputName, bDataTarget ? PCGMetadataDomainID::Data : PCGMetadataDomainID::Elements);

		// A @Data source holds one value whatever the rows; it lands on the default slot, or on one new row.
		auto WriteTargets = [&](auto&& Write) -> bool
		{
			if (bDataTarget)
			{
				return Write(0, PCGDefaultValueKey);
			}

			if (bDataSource)
			{
				return Write(0, OutMetadata->AddEntry());
			}

			bool bAnyWritten = false;
			for (const int32 Row : Rows)
			{
				bAnyWritten |= Write(Row, OutMetadata->AddEntry());
			}
			return bAnyWritten;
		};

		bool bWritten = false;

		if (SourceAttribute && !PCGExMetaHelpers::IsBasicSingleValue(SourceAttribute->GetAttributeDesc()))
		{
			// Structs, arrays and the other extended types have no conversion: they are copied whole or not at all.
			if (Pin.Type != EPCGMetadataTypes::Unknown || PCGExData::FSubSelection(Selector).HasSelection())
			{
				PCGEX_LOG_INVALID_INPUT(Context, FText::Format(LOCTEXT("ExtendedTypeNeedsAuto", "'{0}' holds a struct, array or other extended type: it can only be output whole, with the Auto type."), SourceText))
				return nullptr;
			}

			FPCGMetadataAttributeDesc Desc = SourceAttribute->GetAttributeDesc();
			Desc.Name = OutputName;

			FPCGMetadataDomain* TargetDomain = OutMetadata->GetMetadataDomain(Identifier.MetadataDomain);
			FPCGMetadataAttributeBase* TargetAttribute = TargetDomain ? TargetDomain->CreateAttribute(Desc, SourceAttribute->AllowsInterpolation(), /*bOverrideParent=*/true) : nullptr;

			const TSharedPtr<const IPCGAttributeAccessorKeys> SourceKeys = bDataSource ? nullptr : PCGExMetaHelpers::MakeConstKeys(Data);

			if (TargetAttribute && (bDataSource || SourceKeys))
			{
				bWritten = WriteTargets([&](const int32 Row, const PCGMetadataEntryKey TargetKey)
				{
					PCGMetadataEntryKey SourceKey = PCGExData::Helpers::GetDataValueKey(SourceAttribute);
					if (!bDataSource)
					{
						const PCGMetadataEntryKey* RowKey = nullptr;
						if (!SourceKeys->GetKey<PCGMetadataEntryKey>(Row, RowKey) || !RowKey)
						{
							return false;
						}
						SourceKey = *RowKey;
					}

					return PCGExData::Helpers::PropertyCopyAttribute(SourceAttribute, SourceKey, TargetAttribute, TargetKey);
				});
			}
		}
		else
		{
			EPCGMetadataTypes SourceType = EPCGMetadataTypes::Unknown;
			if (!PCGExData::TryGetType(Selector, Data, SourceType))
			{
				PCGEX_LOG_WARN_SELECTOR_C(Context, Attributes, Selector)
				return nullptr;
			}

			// Read as the sub-selection's own result type (a double for .X), then converted by the write.
			const TSharedPtr<PCGExData::IAttributeBroadcaster> Reader = PCGExData::MakeBroadcaster(Selector, Data, PCGExData::FSubSelection(Selector).GetSubType(SourceType));
			if (Reader)
			{
				bWritten = WriteTargets([&](const int32 Row, const PCGMetadataEntryKey TargetKey)
				{
					return PCGExData::WriteSingle(Reader, PCGExData::FElement(Row), OutMetadata, Identifier, TargetKey, Pin.Type) != nullptr;
				});
			}
		}

		if (!bWritten)
		{
			PCGEX_LOG_INVALID_INPUT(Context, FText::Format(LOCTEXT("ReadFailed", "'{0}' could not be read on one of the inputs."), SourceText))
			return nullptr;
		}

		return OutputSet;
	}
}

#pragma endregion

#pragma region UPCGExAttributesToPinsSettings

#if WITH_EDITOR
EPCGChangeType UPCGExAttributesToPinsSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExAttributesToPinsSettings, Attributes))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGExAttributesToPinsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "The data to read values from.", Required)
	PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "Rows to read, one output row per pick. Unconnected, the first row is read.", Normal, FPCGExDataTypeInfoPicker::AsId())
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExAttributesToPinsSettings::OutputPinProperties() const
{
	TArray<PCGExAttributesToPins::FPin> Pins;
	GetPins(Pins);

	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Reserve(Pins.Num());

	for (const PCGExAttributesToPins::FPin& OutPin : Pins)
	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(OutPin.Label, FPCGDataTypeInfoParam::AsId());
		PCGEX_PIN_TOOLTIP("One attribute set per input, holding only this value.")

		// A valid subtype types the pin; Auto leaves it as any attribute set.
		if (OutPin.Type != EPCGMetadataTypes::Unknown)
		{
			Pin.AllowedTypes.CustomSubtype = static_cast<int32>(OutPin.Type);
		}
	}

	return PinProperties;
}

bool UPCGExAttributesToPinsSettings::GetPins(TArray<PCGExAttributesToPins::FPin>& OutPins) const
{
	OutPins.Reset(Attributes.Num());

	TSet<FName> Labels;
	Labels.Reserve(Attributes.Num());

	bool bAllUnique = true;

	for (const FPCGExAttributeToPinDetails& Entry : Attributes)
	{
		const FName Label = Entry.GetPinLabel();
		if (Label.IsNone())
		{
			continue;
		}

		bool bAlreadyUsed = false;
		Labels.Add(Label, &bAlreadyUsed);
		if (bAlreadyUsed)
		{
			bAllUnique = false;
			continue;
		}

		PCGExAttributesToPins::FPin& Pin = OutPins.Emplace_GetRef();
		Pin.Selector = Entry.GetSourceSelector();
		Pin.Label = Label;
		Pin.OutputName = Entry.GetRemappedName();
		Pin.Type = PCGExAttributesToPins::GetMetadataType(Entry.Type);
	}

	return bAllUnique;
}

PCGEX_INITIALIZE_ELEMENT(AttributesToPins)

#pragma endregion

#pragma region FPCGExAttributesToPinsElement

bool FPCGExAttributesToPinsElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(AttributesToPins)

	if (!Settings->GetPins(Context->Pins))
	{
		PCGEX_LOG_INVALID_INPUT(Context, LOCTEXT("DuplicatePins", "Some entries share their output name with an earlier entry and were ignored."))
	}

	if (Context->Pins.IsEmpty())
	{
		return false;
	}

	// Optional: no picker means the first row.
	PCGExFactories::GetInputFactories(Context, PCGExPickers::Labels::SourcePickersLabel, Context->PickerFactories, {FPCGExDataTypeInfoPicker::AsId()}, /*bRequired=*/false);

	return true;
}

bool FPCGExAttributesToPinsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributesToPinsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AttributesToPins)
	PCGEX_EXECUTION_CHECK

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<int32> Rows;
	for (const FPCGTaggedData& Input : Inputs)
	{
		if (!Input.Data)
		{
			continue;
		}

		PCGExAttributesToPins::GetRows(Context->PickerFactories, PCGExMetaHelpers::GetElementsCount(Input.Data), Rows);

		for (const PCGExAttributesToPins::FPin& Pin : Context->Pins)
		{
			if (UPCGParamData* OutputSet = PCGExAttributesToPins::Extract(Context, Pin, Input.Data, Rows, Settings->bDataDomainToElements))
			{
				Context->StageOutput(OutputSet, Pin.Label, PCGExData::EStaging::None, Input.Tags);
			}
		}
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
