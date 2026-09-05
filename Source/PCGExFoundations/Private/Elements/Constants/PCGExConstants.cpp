// Copyright 2026 Edward Boucher and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Constants/PCGExConstants.h"
#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExDataHelpers.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"

#if WITH_EDITOR
FName UPCGExConstantsSettings::GetEnumName() const
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExConstantListID>())
	{
		return FName(EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(ConstantList)).ToString());
	}
	return NAME_None;
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGExConstantsSettings::GetPreconfiguredInfo() const
{
	const TSet ValuesToSkip = {EPCGExConstantListID::MAX_BOOL, EPCGExConstantListID::ADDITIONAL_VECTORS, EPCGExConstantListID::ADDITIONAL_NUMERICS};

	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExConstantListID>(ValuesToSkip);
}
#endif

void UPCGExConstantsSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExConstantListID>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			ConstantList = static_cast<EPCGExConstantListID>(PreconfigureInfo.PreconfiguredIndex);

			AttributeNameMap.Empty();

			switch (GetOutputType(ConstantList))
			{
			case EPCGExConstantType::Number:
				for (const auto Constant : GetNumericConstantList(ConstantList).Constants)
				{
					AttributeNameMap.Add(Constant.Name, Constant.Name);
				}
				break;
			case EPCGExConstantType::Vector:
				for (const auto Constant : GetVectorConstantList(ConstantList).Constants)
				{
					AttributeNameMap.Add(Constant.Name, Constant.Name);
				}
				break;
			case EPCGExConstantType::Bool:
				for (const auto Constant : GetBooleanConstantList(ConstantList))
				{
					AttributeNameMap.Add(Constant.Name, Constant.Name);
				}
				break;
			}
		}
	}
}

EPCGExConstantType UPCGExConstantsSettings::GetOutputType(const EPCGExConstantListID ListID)
{
	if (ListID <= EPCGExConstantListID::One || ListID > EPCGExConstantListID::ADDITIONAL_NUMERICS)
	{
		return EPCGExConstantType::Number;
	}
	if (ListID >= EPCGExConstantListID::Booleans && ListID < EPCGExConstantListID::MAX_BOOL)
	{
		return EPCGExConstantType::Bool;
	}
	return EPCGExConstantType::Vector;
}

#if WITH_EDITOR
bool UPCGExConstantsSettings::CanEditChange(const FProperty* InProperty) const
{
	const bool ParentVal = Super::CanEditChange(InProperty);

	const FName Prop = InProperty->GetFName();
	const EPCGExConstantType OutputType = GetOutputType(ConstantList);

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantsSettings, NegateOutput))
	{
		return ParentVal && (OutputType != EPCGExConstantType::Bool);
	}

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantsSettings, OutputReciprocal))
	{
		return ParentVal && OutputType == EPCGExConstantType::Number;
	}

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantsSettings, CustomMultiplier))
	{
		return ParentVal && (OutputType != EPCGExConstantType::Bool);
	}

	if (Prop == GET_MEMBER_NAME_CHECKED(UPCGExConstantsSettings, NumericOutputType))
	{
		return ParentVal && (OutputType == EPCGExConstantType::Number);
	}

	return ParentVal;
}
#endif

TArray<FPCGPinProperties> UPCGExConstantsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	const EPCGExConstantType OutputType = GetOutputType(ConstantList);
	// Boolean pins
	if (OutputType == EPCGExConstantType::Bool)
	{
		for (auto List = GetBooleanConstantList(ConstantList);
		     const auto Constant : List)
		{
			PCGEX_PIN_PARAM(Constant.Name, "...", Normal)
		}
	}

	// Vector pins
	else if (OutputType == EPCGExConstantType::Vector)
	{
		for (auto List = GetVectorConstantList(ConstantList).Constants;
		     const auto Constant : List)
		{
			PCGEX_PIN_PARAM(Constant.Name, "...", Normal)
		}
	}
	// Numerics
	else
	{
		for (auto List = GetNumericConstantList(ConstantList).Constants;
		     const auto Constant : List)
		{
			PCGEX_PIN_PARAM(Constant.Name, "...", Normal)
		}
	}


	return PinProperties;
}

FPCGElementPtr UPCGExConstantsSettings::CreateElement() const
{
	return MakeShared<FPCGExConstantsElement>();
}

bool FPCGExConstantsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(Constants)

	EPCGExConstantType OutputType = Settings->GetOutputType(Settings->ConstantList);

	if (OutputType == EPCGExConstantType::Bool)
	{
		auto ToOutput = UPCGExConstantsSettings::GetBooleanConstantList(Settings->ConstantList);

		// NoClear and EditFixedSize should prevent the names being missing on newly placed nodes, but if there's an old
		// graph with constants (or something else goes wrong) this will stop it trying to dereference a null pointer
		bool HasValidOutputNames = Settings->AttributeNameMap.Num() == ToOutput.Num();

		for (const auto Constant : ToOutput)
		{
			StageConstant(Context, Constant.Name, HasValidOutputNames ? *Settings->AttributeNameMap.Find(Constant.Name) : Constant.Name, Constant.Value, Settings);
		}
	}
	else if (OutputType == EPCGExConstantType::Vector)
	{
		auto ToOutput = UPCGExConstantsSettings::GetVectorConstantList(Settings->ConstantList);
		bool HasValidOutputNames = Settings->AttributeNameMap.Num() == ToOutput.Constants.Num();

		for (const auto Constant : ToOutput.Constants)
		{
			StageConstant(Context, Constant.Name, HasValidOutputNames ? *Settings->AttributeNameMap.Find(Constant.Name) : Constant.Name, Constant.Value * Settings->CustomMultiplier * (Settings->NegateOutput ? -1.0 : 1.0), Settings);
		}
	}
	// Numeric constant output
	else
	{
		auto ToOutput = UPCGExConstantsSettings::GetNumericConstantList(Settings->ConstantList);

		bool HasValidOutputNames = Settings->AttributeNameMap.Num() == ToOutput.Constants.Num();

		for (const auto Constant : ToOutput.Constants)
		{
			const FName Name = HasValidOutputNames ? *Settings->AttributeNameMap.Find(Constant.Name) : Constant.Name;
			const double Value = Settings->ApplyNumericValueSettings(Constant.Value);

			switch (Settings->NumericOutputType)
			{
			case EPCGExNumericOutput::Double:
				StageConstant<double>(Context, Constant.Name, Name, Value, Settings);
				break;
			case EPCGExNumericOutput::Float:
				StageConstant<float>(Context, Constant.Name, Name, Value, Settings);
				break;
			case EPCGExNumericOutput::Int32:
				StageConstant<int32>(Context, Constant.Name, Name, Value, Settings);
				break;
			case EPCGExNumericOutput::Int64:
				StageConstant<int64>(Context, Constant.Name, Name, Value, Settings);
				break;
			}
		}
	}

	Context->Done();
	return Context->TryComplete();
}
