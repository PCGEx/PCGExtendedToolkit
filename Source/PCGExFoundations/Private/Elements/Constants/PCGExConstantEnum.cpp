// Copyright 2026 Edward Boucher and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Constants/PCGExConstantEnum.h"

#include "PCGComponent.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#if WITH_EDITOR
FString UPCGExConstantEnumSettings::GetDisplayName() const
{
	const FName Name = GetEnumName();
	if (Name.IsNone())
	{
		return TEXT("...");
	}

	if (OutputMode == EPCGExEnumOutputMode::EEOM_Single && SelectedEnum.IsValid())
	{
		return Name.ToString() + "::" + SelectedEnum.Class->GetDisplayNameTextByValue(SelectedEnum.Value).BuildSourceString() + " (" + FString::FromInt(SelectedEnum.Value) + ")";
	}

	return Name.ToString();
}
#endif

// Adapted from similar handling in PCGSwitch.cpp
void UPCGExConstantEnumSettings::PostLoad()
{
	Super::PostLoad();

	CachePinLabels();
	if (EnabledExportValues.Num() == 0)
	{
		FillEnabledExportValues();
	}

#if WITH_EDITOR
	if (UPCGNode* OuterNode = Cast<UPCGNode>(GetOuter()))
	{
		TArray<UPCGPin*> SerializedOutputPins = OuterNode->GetOutputPins();

		if (SerializedOutputPins.Num() == CachedPinLabels.Num())
		{
			for (int32 i = 0; i < CachedPinLabels.Num(); ++i)
			{
				if (SerializedOutputPins[i] && SerializedOutputPins[i]->Properties.Label != CachedPinLabels[i])
				{
					OuterNode->RenameOutputPin(SerializedOutputPins[i]->Properties.Label, CachedPinLabels[i], /*bBroadcastUpdate=*/false);
				}
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UPCGExConstantEnumSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 15)
	{
		// Legacy nodes had two sources: a free-form UEnum picker (Picker mode) and an
		// FPCGExEnumSelector (Selector mode). FPCGExEnumSelector now handles both native
		// and Blueprint enums, so we collapse to a single field. Picker mode never stored
		// a value, so the migrated SelectedEnum.Value stays at its default of 0.
		if (Source_DEPRECATED == EPCGExEnumConstantSourceType::Picker && PickerEnum_DEPRECATED && !SelectedEnum.Class)
		{
			SelectedEnum.Class = PickerEnum_DEPRECATED;
		}
		Source_DEPRECATED = EPCGExEnumConstantSourceType::Selector;
		PickerEnum_DEPRECATED = nullptr;
	}
	Super::PCGExApplyDeprecation(InOutNode);
}

// Adapted from similar handling in PCGSwitch.cpp
void UPCGExConstantEnumSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName Prop = PropertyChangedEvent.GetMemberPropertyName();

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, SelectedEnum) || Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, OutputMode) || Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, OutputType))
	{
		CachePinLabels();
	}

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, SelectedEnum))
	{
		if (GetEnumClass())
		{
			FillEnabledExportValues();
		}
	}
}
#endif

// This does not exist in 5.4
void UPCGExConstantEnumSettings::OnOverrideSettingsDuplicatedInternal(bool bSkippedPostLoad)
{
	Super::OnOverrideSettingsDuplicatedInternal(bSkippedPostLoad);
	if (bSkippedPostLoad)
	{
		CachePinLabels();
		if (EnabledExportValues.Num() == 0)
		{
			FillEnabledExportValues();
		}
	}
}

TObjectPtr<UEnum> UPCGExConstantEnumSettings::GetEnumClass() const
{
	return SelectedEnum.Class;
}

void UPCGExConstantEnumSettings::FillEnabledExportValues()
{
	EnabledExportValues.Empty();
	for (const auto Value : GetEnumValueMap())
	{
		EnabledExportValues.Emplace(Value.Get<1>(), true);
	}
}

void UPCGExConstantEnumSettings::CachePinLabels()
{
	CachedPinLabels.Empty();
	Algo::Transform(OutputPinProperties(), CachedPinLabels, [](const FPCGPinProperties& Property)
	{
		return Property.Label;
	});
}

TArray<PCGExConstantEnumConstants::FMapping> UPCGExConstantEnumSettings::GetEnumValueMap() const
{
	// Note: arguably this should be <FName, FString, int64>, but:
	// - pin properties expect a name rather than a string
	// - the formatting in the table view is weird if you have a name next to a string
	// - PCG Switch behaves like this
	// ...so we're going to convert the description into a name and hope there aren't any emojis

	const TObjectPtr<UEnum> EnumClass = GetEnumClass();

	TArray<PCGExConstantEnumConstants::FMapping> Out;
	if (!EnumClass)
	{
		return Out;
	}

	// Adapted from PCGControlFlow.cpp
	// -1 to bypass the MAX value
	for (int32 Index = 0; EnumClass && Index < EnumClass->NumEnums() - 1; ++Index)
	{
		bool bHidden = false;
#if WITH_EDITOR
		bHidden = EnumClass->HasMetaData(TEXT("Hidden"), Index) || EnumClass->HasMetaData(TEXT("Spacer"), Index);
#endif

		if (!bHidden)
		{
			Out.Add({
				StripEnumNamespaceFromKey
				? // Key
				FName(EnumClass->GetNameStringByIndex(Index))
				: EnumClass->GetNameByIndex(Index),
				FName(EnumClass->GetDisplayNameTextByIndex(Index).BuildSourceString()), // Description
				EnumClass->GetValueByIndex(Index),                                      // Value
				Index                                                                   // Index
			});
		}
	}

	return Out;
}

FName UPCGExConstantEnumSettings::GetEnumName() const
{
	if (const TObjectPtr<UEnum> E = GetEnumClass())
	{
		return FName(E->GetName());
	}
	return FName("(No Source)");
}

#if WITH_EDITOR
EPCGChangeType UPCGExConstantEnumSettings::GetChangeTypeForProperty(const FName& PropName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropName);

	if (PropName == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, bEnabled) || PropName == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, SelectedEnum) || PropName == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, OutputMode) || PropName == GET_MEMBER_NAME_CHECKED(UPCGExConstantEnumSettings, OutputType))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGExConstantEnumSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	const TObjectPtr<UEnum> EnumClass = GetEnumClass();

	if (!EnumClass)
	{
		return PinProperties;
	}
	const auto EnumName = GetEnumName();

	FText ToolTip = FText::GetEmpty();

	// It's this or a lambda, and a lambda is probably overkill 
#define MAKE_TOOLTIP_FOR_VALUE(_KEY, _VALUE) FText::FromString(_KEY.ToString() + " (" + FString::FromInt(_VALUE) + ")");

	switch (OutputMode)
	{
	case EPCGExEnumOutputMode::EEOM_Single:
		ToolTip = MAKE_TOOLTIP_FOR_VALUE(EnumClass->GetNameByValue(SelectedEnum.Value), SelectedEnum.Value);
		PinProperties.Emplace(PCGExConstantEnumConstants::SingleOutputPinName, EPCGDataType::Param, true, false, ToolTip);
		break;

	case EPCGExEnumOutputMode::EEOM_All:
	case EPCGExEnumOutputMode::EEOM_Selection:
		ToolTip = FText::FromName(EnumName);
		PinProperties.Emplace(PCGExConstantEnumConstants::SingleOutputPinName, EPCGDataType::Param, true, false, ToolTip);
		break;

	case EPCGExEnumOutputMode::EEOM_SelectionToMultiplePins:
		for (const PCGExConstantEnumConstants::FMapping& Mapping : GetEnumValueMap())
		{
			if (EnabledExportValues.Contains(Mapping.Get<1>()) && *EnabledExportValues.Find(Mapping.Get<1>()))
			{
				ToolTip = MAKE_TOOLTIP_FOR_VALUE(Mapping.Get<0>(), Mapping.Get<2>());
				PinProperties.Emplace(Mapping.Get<1>(), EPCGDataType::Param, true, false, ToolTip);
			}
		}
		break;

	case EPCGExEnumOutputMode::EEOM_AllToMultiplePins: default:
		// -1 to bypass the MAX value
		for (const PCGExConstantEnumConstants::FMapping& Mapping : GetEnumValueMap())
		{
			ToolTip = MAKE_TOOLTIP_FOR_VALUE(Mapping.Get<0>(), Mapping.Get<2>());
			PinProperties.Emplace(Mapping.Get<1>(), EPCGDataType::Param, true, false, ToolTip);
		}
	}

	// Output bitmask last
	if (bOutputFlags)
	{
		PCGEX_PIN_PARAM(PCGExConstantEnumConstants::BitflagOutputPinName, "Flags representing the current selection within the enum", Required);
	}

	return PinProperties;
#undef MAKE_TOOLTIP_FOR_VALUE
}

FPCGElementPtr UPCGExConstantEnumSettings::CreateElement() const
{
	return MakeShared<FPCGExConstantEnumElement>();
}

bool FPCGExConstantEnumElement::Boot(FPCGExContext* InContext) const
{
	const UPCGExConstantEnumSettings* Settings = InContext->GetInputSettings<UPCGExConstantEnumSettings>();
	check(Settings);
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	// Validating names, will throw an error
	// This is in an lambda because the macro returns false, which would have the node run forever
	if (Settings->OutputEnumKeys)
	{
		PCGEX_VALIDATE_NAME(Settings->KeyAttribute)
	}
	if (Settings->OutputEnumDescriptions)
	{
		PCGEX_VALIDATE_NAME(Settings->DescriptionAttribute)
	}
	if (Settings->OutputEnumValues)
	{
		PCGEX_VALIDATE_NAME(Settings->ValueOutputAttribute)
	}
	if (Settings->bOutputFlags)
	{
		PCGEX_VALIDATE_NAME(Settings->FlagsName)
	}

	const TObjectPtr<UEnum> EnumClass = Settings->GetEnumClass();

	// No class selected, so can't output anything
	if (!EnumClass)
	{
		return false;
	}

	// No data selected to output
	if (!Settings->OutputEnumValues && !Settings->OutputEnumKeys && !Settings->OutputEnumDescriptions)
	{
		return false;
	}

	return true;
}

bool FPCGExConstantEnumElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(ConstantEnum)


	const TArray<PCGExConstantEnumConstants::FMapping> Unfiltered = Settings->GetEnumValueMap();
	TArray<PCGExConstantEnumConstants::FMapping> Filtered;

	FPCGExBitmask Bitflags;
	Bitflags.Mode = EPCGExBitmaskMode::Individual;
	Bitflags.Mutations.SetNum(Unfiltered.Num());
	for (int i = 0; i < Unfiltered.Num(); i++)
	{
		Bitflags.Mutations[i].BitIndex = Settings->FlagBitOffset + i;
		Bitflags.Mutations[i].bValue = false;
	}


	switch (Settings->OutputMode)
	{
	case EPCGExEnumOutputMode::EEOM_Single:
	{
		for (int i = 0; i < Unfiltered.Num(); i++)
		{
			if (Unfiltered[i].Get<2>() == Settings->SelectedEnum.Value)
			{
				// Using the single pin so connections don't break when the user changes the value
				StageEnumValuesSinglePin(Context, Settings, {Unfiltered[i]}, Bitflags);
				break;
			}
		}
	}
	break;

	// Output everything
	case EPCGExEnumOutputMode::EEOM_All:
		StageEnumValuesSinglePin(Context, Settings, Unfiltered, Bitflags);
		break;

	case EPCGExEnumOutputMode::EEOM_Selection:
	case EPCGExEnumOutputMode::EEOM_SelectionToMultiplePins:
		Filtered = Settings->GetEnumValueMap().FilterByPredicate([&](const PCGExConstantEnumConstants::FMapping& V)
		{
			return Settings->EnabledExportValues.Contains(V.Get<1>()) && *Settings->EnabledExportValues.Find(V.Get<1>());
		});

		if (Settings->OutputMode == EPCGExEnumOutputMode::EEOM_Selection)
		{
			StageEnumValuesSinglePin(Context, Settings, Filtered, Bitflags);
		}
		else
		{
			StageEnumValuesSeparatePins(Context, Settings, Filtered, Bitflags);
		}

		break;

	// Output everything, but on different pins
	case EPCGExEnumOutputMode::EEOM_AllToMultiplePins: default:
		StageEnumValuesSeparatePins(Context, Settings, Unfiltered, Bitflags);
	}

	StageBitFlags(Context, Settings, Bitflags);

	Context->Done();
	return Context->TryComplete();
}

void FPCGExConstantEnumElement::StageEnumValuesSeparatePins(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, const TArray<PCGExConstantEnumConstants::FMapping>& ValueData, FPCGExBitmask& OutBitflags)
{
	for (const PCGExConstantEnumConstants::FMapping& T : ValueData)
	{
		OutBitflags.Mutations[T.Get<3>()].bValue = true;

		UPCGParamData* OutputData = InContext->ManagedObjects->New<UPCGParamData>();

		FPCGMetadataAttribute<FName>* KeyAttrib = nullptr;
		FPCGMetadataAttribute<FName>* DescriptionAttrib = nullptr;
		FPCGMetadataAttribute<int64>* ValueAttrib = nullptr;

		if (Settings->OutputEnumKeys)
		{
			KeyAttrib = OutputData->Metadata->CreateAttribute<FName>(Settings->KeyAttribute, NAME_None, false, false);
		}

		if (Settings->OutputEnumDescriptions)
		{
			DescriptionAttrib = OutputData->Metadata->CreateAttribute<FName>(Settings->DescriptionAttribute, NAME_None, false, false);
		}

		if (Settings->OutputEnumValues)
		{
			ValueAttrib = OutputData->Metadata->CreateAttribute<int64>(Settings->ValueOutputAttribute, 0, true, false);
		}

		const auto Entry = OutputData->Metadata->AddEntry();
		if (KeyAttrib)
		{
			KeyAttrib->SetValue(Entry, T.Get<0>());
		}
		if (DescriptionAttrib)
		{
			DescriptionAttrib->SetValue(Entry, T.Get<1>());
		}
		if (ValueAttrib)
		{
			ValueAttrib->SetValue(Entry, T.Get<2>());
		}

		InContext->StageOutput(OutputData, T.Get<1>(), PCGExData::EStaging::Managed);
	}
}

void FPCGExConstantEnumElement::StageEnumValuesSinglePin(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, const TArray<PCGExConstantEnumConstants::FMapping>& ValueData, FPCGExBitmask& OutBitflags)
{
	UPCGParamData* OutputData = InContext->ManagedObjects->New<UPCGParamData>();

	FPCGMetadataAttribute<FName>* KeyAttrib = nullptr;
	FPCGMetadataAttribute<int64>* ValueAttrib = nullptr;
	FPCGMetadataAttribute<FName>* DescriptionAttrib = nullptr;


	if (Settings->OutputEnumKeys)
	{
		KeyAttrib = OutputData->Metadata->CreateAttribute<FName>(Settings->KeyAttribute, NAME_None, false, false);
	}

	if (Settings->OutputEnumDescriptions)
	{
		DescriptionAttrib = OutputData->Metadata->CreateAttribute<FName>(Settings->DescriptionAttribute, NAME_None, false, false);
	}

	if (Settings->OutputEnumValues)
	{
		ValueAttrib = OutputData->Metadata->CreateAttribute<int64>(Settings->ValueOutputAttribute, 0, true, false);
	}

	for (const PCGExConstantEnumConstants::FMapping& T : ValueData)
	{
		OutBitflags.Mutations[T.Get<3>()].bValue = true;

		const auto Entry = OutputData->Metadata->AddEntry();
		if (KeyAttrib)
		{
			KeyAttrib->SetValue(Entry, T.Get<0>());
		}
		if (DescriptionAttrib)
		{
			DescriptionAttrib->SetValue(Entry, T.Get<1>());
		}
		if (ValueAttrib)
		{
			ValueAttrib->SetValue(Entry, T.Get<2>());
		}
	}

	InContext->StageOutput(OutputData, PCGExConstantEnumConstants::SingleOutputPinName, PCGExData::EStaging::Managed);
}

void FPCGExConstantEnumElement::StageBitFlags(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, FPCGExBitmask& OutBitflags)
{
	if (!Settings->bOutputFlags)
	{
		return;
	}

	UPCGParamData* OutputData = InContext->ManagedObjects->New<UPCGParamData>();
	OutputData->Metadata->CreateAttribute<int64>(Settings->FlagsName, OutBitflags.Get(), false, false);
	OutputData->Metadata->AddEntry();

	InContext->StageOutput(OutputData, PCGExConstantEnumConstants::BitflagOutputPinName, PCGExData::EStaging::Managed);
}
