// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Pickers/PCGExPickerAttributeSetRanges.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCreatePickerConstantSet"
#define PCGEX_NAMESPACE CreatePickerConstantSet

PCGEX_PICKER_BOILERPLATE(AttributeSetRanges, {}, {})

#if WITH_EDITOR
FString UPCGExPickerAttributeSetRangesSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Pick Set(s)");

	if (Config.bTreatAsNormalized)
	{
		//DisplayName += FString::Printf(TEXT("%.2f"), Config.RelativeIndex);
	}
	else
	{
		//DisplayName += FString::Printf(TEXT("%d"), Config.DiscreteIndex);
	}

	return DisplayName;
}
#endif

bool UPCGExPickerAttributeSetRangesFactory::GetUniqueRanges(FPCGExContext* InContext, const FName InPinLabel, const FPCGExPickerAttributeSetRangesConfig& InConfig, TArray<FPCGExPickerConstantRangeConfig>& OutRanges, const bool bQuiet)
{
	TArray<TSharedPtr<PCGExData::FFacade>> Facades;
	if (!TryGetFacades(InContext, InPinLabel, Facades, false, true))
	{
		if (!bQuiet)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No valid data was found."));
		}
		return false;
	}

	TSet<FVector2D> UniqueRanges;

	for (const TSharedPtr<PCGExData::FFacade>& Facade : Facades)
	{
		if (InConfig.Attributes.IsEmpty())
		{
			const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(Facade->Source->GetIn()->Metadata);
			if (Infos->Identities.IsEmpty())
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some input have no attributes."));
				continue;
			}

			const TSharedPtr<PCGExData::TAttributeBroadcaster<FVector2D>> Values = PCGExData::MakeTypedBroadcaster<FVector2D>(Infos->Identities[0].Name, Facade->Source);
			if (!Values)
			{
				continue;
			}
			Values->GrabUniqueValues(UniqueRanges);
		}
		else
		{
			for (const FPCGAttributePropertyInputSelector& Selector : InConfig.Attributes)
			{
				const TSharedPtr<PCGExData::TAttributeBroadcaster<FVector2D>> Values = PCGExData::MakeTypedBroadcaster<FVector2D>(Selector, Facade->Source);
				if (!Values)
				{
					continue;
				}
				Values->GrabUniqueValues(UniqueRanges);
			}
		}
	}

	if (UniqueRanges.IsEmpty())
	{
		return false;
	}

	for (const FVector2D& Range : UniqueRanges)
	{
		FPCGExPickerConstantRangeConfig& RangeConfig = OutRanges.Emplace_GetRef();

		RangeConfig.bTreatAsNormalized = InConfig.bTreatAsNormalized;
		RangeConfig.TruncateMode = InConfig.TruncateMode;
		RangeConfig.Safety = InConfig.Safety;

		RangeConfig.DiscreteStartIndex = Range.X;
		RangeConfig.RelativeStartIndex = Range.X;

		RangeConfig.DiscreteEndIndex = Range.Y;
		RangeConfig.RelativeEndIndex = Range.Y;

		RangeConfig.Sanitize();
	}

	return true;
}

void UPCGExPickerAttributeSetRangesFactory::AddPicks(const int32 InNum, TSet<int32>& OutPicks) const
{
	for (const FPCGExPickerConstantRangeConfig& RangeConfig : Ranges)
	{
		UPCGExPickerConstantRangeFactory::AddPicksFromConfig(RangeConfig, InNum, OutPicks);
	}
}

PCGExFactories::EPreparationResult UPCGExPickerAttributeSetRangesFactory::InitInternalData(FPCGExContext* InContext)
{
	PCGExFactories::EPreparationResult Result = Super::InitInternalData(InContext);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}
	return GetUniqueRanges(InContext, FName("Ranges"), Config, Ranges) ? Result : PCGExFactories::EPreparationResult::Fail;
}

TArray<FPCGPinProperties> UPCGExPickerAttributeSetRangesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY(FName("Ranges"), "Data to read attribute from", Required)
	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
