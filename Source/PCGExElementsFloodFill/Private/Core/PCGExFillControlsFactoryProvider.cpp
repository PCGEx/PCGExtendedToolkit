// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExFillControlsFactoryProvider.h"

#define LOCTEXT_NAMESPACE "PCGExCreateFillControls"
#define PCGEX_NAMESPACE CreateFillControls

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoFillControl, UPCGExFillControlsFactoryData)

void FPCGExFillControlConfigBase::Init()
{
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	return nullptr; // Create FillControl operation
}

UPCGExFactoryData* UPCGExFillControlsFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return InFactory;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
