// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"

#define LOCTEXT_NAMESPACE "PCGExCreateNoise3D"
#define PCGEX_NAMESPACE CreateNoise3D

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoNoise3D, UPCGExNoise3DFactoryData)

void FPCGExNoise3DConfigBase::Init()
{
	RemapLUT = RemapCurveLookup.MakeLookup(true, LocalRemapCurve, nullptr);
}

bool UPCGExNoise3DFactoryData::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}
	return true;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExNoise3DOperation> Operation = CreateOperationInternal(InContext);
	if (Operation)
	{
		Operation->PostInit();
	}
	return Operation;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryData::CreateOperationInternal(FPCGExContext* InContext) const
{
	return nullptr;
}

UPCGExFactoryData* UPCGExNoise3DFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return InFactory;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
