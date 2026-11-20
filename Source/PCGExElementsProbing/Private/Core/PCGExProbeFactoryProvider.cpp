// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExProbeFactoryProvider.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExProbeOperation.h"
#include "PCGExVersion.h"

#define LOCTEXT_NAMESPACE "PCGExCreateProbe"
#define PCGEX_NAMESPACE CreateProbe

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoProbe, UPCGExProbeFactoryData)

TSharedPtr<FPCGExProbeOperation> UPCGExProbeFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	return nullptr; // Create probe operation
}

FName UPCGExProbeFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGExClusters::Labels::OutputProbeLabel;
}

UPCGExFactoryData* UPCGExProbeFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return Super::CreateFactory(InContext, InFactory);
}

#if WITH_EDITOR
void UPCGExProbeFactoryProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		if (const FPCGExProbeConfigBase* BaseConfig = GetMutableBaseConfig())
		{
			BaseConfig->RenamePins(this, InOutNode);
		}
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeFactoryProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		if (FPCGExProbeConfigBase* BaseConfig = GetMutableBaseConfig())
		{
			BaseConfig->ApplyDeprecation();
		}
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
