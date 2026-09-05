// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExShapeBuilderFactoryProvider.h"
#include "Core/PCGExShapeBuilderOperation.h"

#define LOCTEXT_NAMESPACE "PCGExCreateShapeBuilder"
#define PCGEX_NAMESPACE CreateShapeBuilder

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoShape, UPCGExShapeBuilderFactoryData)

TSharedPtr<FPCGExShapeBuilderOperation> UPCGExShapeBuilderFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	return nullptr; // Create shape builder operation
}

#if WITH_EDITOR
void UPCGExShapeBuilderFactoryProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		// Rewire Resolution
		PCGEX_SHORTHAND_RENAME_PIN(ResolutionAttribute, ResolutionConstant, Resolution)

		PCGEX_SHORTHAND_RENAME_PIN(ResolutionAttribute, ResolutionConstantVector, ResolutionVector)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

UPCGExFactoryData* UPCGExShapeBuilderFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return Super::CreateFactory(InContext, InFactory);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
