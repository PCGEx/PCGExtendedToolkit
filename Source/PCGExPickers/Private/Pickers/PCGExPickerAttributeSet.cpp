// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Pickers/PCGExPickerAttributeSet.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExCreatePickerConstantSet"
#define PCGEX_NAMESPACE CreatePickerConstantSet

PCGEX_PICKER_BOILERPLATE(AttributeSet, {}, {})

#if WITH_EDITOR
FString UPCGExPickerAttributeSetSettings::GetDisplayName() const
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

void UPCGExPickerAttributeSetFactory::AddPicks(const int32 InNum, TSet<int32>& OutPicks) const
{
	int32 TargetIndex = 0;
	const int32 MaxIndex = InNum - 1;

	if (Config.bTreatAsNormalized)
	{
		OutPicks.Reserve(OutPicks.Num() + RelativePicks.Num());
		for (const double Pick : RelativePicks)
		{
			TargetIndex = PCGExMath::TruncateDbl(static_cast<double>(MaxIndex) * Pick, Config.TruncateMode);

			if (TargetIndex < 0)
			{
				TargetIndex = InNum + TargetIndex;
			}
			TargetIndex = PCGExMath::SanitizeIndex(TargetIndex, MaxIndex, Config.Safety);

			if (FMath::IsWithin(TargetIndex, 0, InNum))
			{
				OutPicks.Add(TargetIndex);
			}
		}
	}
	else
	{
		OutPicks.Reserve(OutPicks.Num() + DiscretePicks.Num());
		for (const int32 Pick : DiscretePicks)
		{
			TargetIndex = Pick;

			if (TargetIndex < 0)
			{
				TargetIndex = InNum + TargetIndex;
			}
			TargetIndex = PCGExMath::SanitizeIndex(TargetIndex, MaxIndex, Config.Safety);

			if (FMath::IsWithin(TargetIndex, 0, InNum))
			{
				OutPicks.Add(TargetIndex);
			}
		}
	}
}

PCGExFactories::EPreparationResult UPCGExPickerAttributeSetFactory::InitInternalData(FPCGExContext* InContext)
{
	PCGExFactories::EPreparationResult Result = Super::InitInternalData(InContext);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	TArray<TSharedPtr<PCGExData::FFacade>> Facades;
	if (!TryGetFacades(InContext, FName("Indices"), Facades, false, true))
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No valid data was found for indices."));
		return PCGExFactories::EPreparationResult::Fail;
	}

	if (Config.bTreatAsNormalized)
	{
		TSet<double> UniqueIndices;
		for (const TSharedPtr<PCGExData::FFacade>& Facade : Facades)
		{
			if (Config.Attributes.IsEmpty())
			{
				const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(Facade->Source->GetIn()->Metadata);
				if (Infos->Attributes.IsEmpty())
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some input have no attributes."));
					continue;
				}

				const TSharedPtr<PCGExData::TAttributeBroadcaster<double>> Values = PCGExData::MakeTypedBroadcaster<double>(Infos->Attributes[0]->Name, Facade->Source);
				if (!Values)
				{
					continue;
				}
				Values->GrabUniqueValues(UniqueIndices);
			}
			else
			{
				for (const FPCGAttributePropertyInputSelector& Selector : Config.Attributes)
				{
					const TSharedPtr<PCGExData::TAttributeBroadcaster<double>> Values = PCGExData::MakeTypedBroadcaster<double>(Selector, Facade->Source);
					if (!Values)
					{
						continue;
					}
					Values->GrabUniqueValues(UniqueIndices);
				}
			}
		}

		RelativePicks = UniqueIndices.Array();
	}
	else
	{
		TSet<int32> UniqueIndices;
		for (const TSharedPtr<PCGExData::FFacade>& Facade : Facades)
		{
			if (Config.Attributes.IsEmpty())
			{
				const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(Facade->Source->GetIn()->Metadata);
				if (Infos->Attributes.IsEmpty())
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some input have no attributes."));
					continue;
				}

				const TSharedPtr<PCGExData::TAttributeBroadcaster<int32>> Values = PCGExData::MakeTypedBroadcaster<int32>(Infos->Attributes[0]->Name, Facade->Source);
				if (!Values)
				{
					continue;
				}
				Values->GrabUniqueValues(UniqueIndices);
			}
			else
			{
				for (const FPCGAttributePropertyInputSelector& Selector : Config.Attributes)
				{
					const TSharedPtr<PCGExData::TAttributeBroadcaster<int32>> Values = PCGExData::MakeTypedBroadcaster<int32>(Selector, Facade->Source);
					if (!Values)
					{
						continue;
					}
					Values->GrabUniqueValues(UniqueIndices);
				}
			}
		}

		DiscretePicks = UniqueIndices.Array();
	}

	return Result;
}

TArray<FPCGPinProperties> UPCGExPickerAttributeSetSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY(FName("Indices"), "Data to read attribute from", Required)
	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
