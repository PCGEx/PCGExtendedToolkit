// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Partition/PCGExModularPartitionByValues.h"

#include "Containers/PCGExManagedObjects.h"


#define LOCTEXT_NAMESPACE "PCGExModularPartitionByValues"
#define PCGEX_NAMESPACE ModularPartitionByValues

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoPartitionRule, UPCGExPartitionRule)

UPCGExFactoryData* UPCGExPartitionRuleProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExPartitionRule* NewFactory = InContext->ManagedObjects->New<UPCGExPartitionRule>();
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExPartitionRuleProviderSettings::GetDisplayName() const
{
	return Config.GetDisplayName();
}
#endif

TArray<FPCGPinProperties> UPCGExModularPartitionByValuesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(TEXT("PartitionRules"), "Plug partitions rules here.", Required, FPCGExDataTypeInfoPartitionRule::AsId())
	return PinProperties;
}

bool UPCGExModularPartitionByValuesSettings::GetPartitionRules(FPCGExContext* InContext, TArray<FPCGExPartitonRuleConfig>& OutRules) const
{
	TArray<TObjectPtr<const UPCGExPartitionRule>> Factories;
	if (!PCGExFactories::GetInputFactories(InContext, TEXT("PartitionRules"), Factories, {FPCGExDataTypeInfoPartitionRule::AsId()}))
	{
		return false;
	}

	for (const UPCGExPartitionRule* Factory : Factories)
	{
		OutRules.Add(Factory->Config);
	}

	return true;
}
