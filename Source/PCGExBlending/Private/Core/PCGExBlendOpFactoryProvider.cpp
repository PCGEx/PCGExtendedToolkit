// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlendOpFactoryProvider.h"

#include "PCGExVersion.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"

#define LOCTEXT_NAMESPACE "PCGExCreateAttributeBlend"
#define PCGEX_NAMESPACE CreateAttributeBlend

#if WITH_EDITOR
void UPCGExBlendOpFactoryProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameInputPin(FName("Constant A"), PCGExBlending::Labels::SourceConstantA);
	InOutNode->RenameInputPin(FName("Constant B"), PCGExBlending::Labels::SourceConstantB);

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.Weighting.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExBlendOpFactoryProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.Weighting.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

void UPCGExBlendOpFactoryProviderSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Config.bRequiresWeight = Config.BlendMode == EPCGExABBlendingType::Lerp || Config.BlendMode == EPCGExABBlendingType::Weight || Config.BlendMode == EPCGExABBlendingType::WeightedSubtract || Config.BlendMode == EPCGExABBlendingType::WeightedAdd;

	FName Prop = PropertyChangedEvent.GetMemberPropertyName();
	if (Prop == GET_MEMBER_NAME_CHECKED(FPCGExAttributeBlendConfig, OperandASource) && Config.OperandASource == EPCGExOperandSource::Constant)
	{
	}
	else if (Prop == GET_MEMBER_NAME_CHECKED(FPCGExAttributeBlendConfig, OperandBSource) && Config.OperandBSource == EPCGExOperandSource::Constant)
	{
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UPCGExBlendOpFactoryProviderSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceConstantA && Config.OperandASource == EPCGExOperandSource::Constant)
	{
		return true;
	}
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceConstantB && Config.OperandBSource == EPCGExOperandSource::Constant)
	{
		return true;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExBlendOpFactoryProviderSettings::GetPreconfiguredInfo() const
{
	const TSet ValuesToSkip = {EPCGExABBlendingType::None};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExABBlendingType>(ValuesToSkip, FTEXT("Blend : {0}"));
}
#endif

void UPCGExBlendOpFactoryProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExABBlendingType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Config.BlendMode = static_cast<EPCGExABBlendingType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

TArray<FPCGPinProperties> UPCGExBlendOpFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY_SINGLE(PCGExBlending::Labels::SourceConstantA, "Data used to read a constant from. Will read from the first element of the first data.", Advanced)

	if (Config.bUseOperandB)
	{
		PCGEX_PIN_ANY_SINGLE(PCGExBlending::Labels::SourceConstantB, "Data used to read a constant from. Will read from the first element of the first data.", Advanced)
	}

	return PinProperties;
}

UPCGExFactoryData* UPCGExBlendOpFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExBlendOpFactory* NewFactory = InContext->ManagedObjects->New<UPCGExBlendOpFactory>();
	NewFactory->Priority = Priority;
	NewFactory->Config = Config;
	NewFactory->Config.Init();

	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExBlendOpFactoryProviderSettings::GetDisplayName() const
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExABBlendingType>())
	{
		FString Str = FString::Printf(TEXT("%s %s"), *EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(Config.BlendMode)).ToString(), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA));

		switch (Config.OutputMode)
		{
		case EPCGExBlendOpOutputMode::SameAsA:
			break;
		case EPCGExBlendOpOutputMode::SameAsB:
			if (Config.bUseOperandB)
			{
				Str += FString::Printf(TEXT(" ⇌ %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB));
			}
			else
			{
				Str += FString::Printf(TEXT(" → %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB));
			}
			break;
		case EPCGExBlendOpOutputMode::New:
			if (Config.bUseOperandB)
			{
				Str += FString::Printf(TEXT(" & %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB));
			}
			else
			{
				Str += FString::Printf(TEXT(" → %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OutputTo));
			}
			break;
		case EPCGExBlendOpOutputMode::Transient:
			if (Config.bUseOperandB)
			{
				Str += FString::Printf(TEXT(" & %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB));
			}
			Str += FString::Printf(TEXT(" ⇢ %s"), *PCGExMetaHelpers::GetSelectorDisplayName(Config.OutputTo));
			break;
		}
		return Str;
	}

	return TEXT("PCGEx | Blend Op");
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
