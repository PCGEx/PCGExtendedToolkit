// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExFilterFactoryProvider.h"

#include "PCGExFilterCommon.h"
#include "Containers/PCGExManagedObjects.h"
#include "Filters/Points/PCGExConstantFilter.h"

#define LOCTEXT_NAMESPACE "PCGExCreateFilter"
#define PCGEX_NAMESPACE PCGExCreateFilter

#if WITH_EDITOR
FString UPCGExFilterProviderSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

UPCGExFilterProviderSettings::UPCGExFilterProviderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Priority = GetDefaultPriority();
}

FName UPCGExFilterProviderSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputFilterLabel;
}

UPCGExFactoryData* UPCGExFilterProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExPointFilterFactoryData* NewFactory = Cast<UPCGExPointFilterFactoryData>(InFactory);
	NewFactory->MissingDataPolicy = MissingDataPolicy;
	NewFactory->Priority = Priority;

	return Super::CreateFactory(InContext, NewFactory);
}

// Called when the provider's required input data is missing. Instead of canceling
// the node entirely, substitutes a constant filter (always-pass or always-fail)
// based on MissingDataPolicy. This allows downstream nodes to keep running with
// a predictable fallback behavior rather than producing no output.
bool UPCGExFilterProviderSettings::ShouldCancel(FPCGExFactoryProviderContext* InContext, PCGExFactories::EPreparationResult InResult) const
{
	if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error)
	{
		return Super::ShouldCancel(InContext, InResult);
	}

	UPCGExConstantFilterFactory* NewFactory = InContext->ManagedObjects->New<UPCGExConstantFilterFactory>();

	NewFactory->Priority = Priority;
	NewFactory->Config.bInvert = false;
	NewFactory->Config.Value = MissingDataPolicy == EPCGExFilterNoDataFallback::Pass;

	if (InContext->OutFactory)
	{
		InContext->ManagedObjects->Destroy(InContext->OutFactory);
	}
	InContext->OutFactory = NewFactory;

	return false;
}

#if WITH_EDITOR
void UPCGExFilterCollectionProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameOutputPin(FName("C-Filter"), PCGExFilters::Labels::OutputColFilterLabel);
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

FName UPCGExFilterCollectionProviderSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputColFilterLabel;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
