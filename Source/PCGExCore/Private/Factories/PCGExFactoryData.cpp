// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExFactoryData.h"

#include "Data/PCGExPointIO.h"
#include "Tasks/Task.h"

#define LOCTEXT_NAMESPACE "PCGExFactoryProvider"
#define PCGEX_NAMESPACE PCGExFactoryProvider

PCG_DEFINE_TYPE_INFO(FPCGExFactoryDataTypeInfo, UPCGExFactoryData)

void UPCGExParamDataBase::OutputConfigToMetadata()
{
}

bool UPCGExFactoryData::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	return bCleanupConsumableAttributes;
}

bool UPCGExFactoryData::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	return bCleanupConsumableAttributes;
}

void UPCGExFactoryData::RegisterAssetDependencies(FPCGExContext* InContext) const
{
}

void UPCGExFactoryData::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
}

void UPCGExFactoryData::AddDataDependency(const UPCGData* InData)
{
	bool bAlreadyInSet = false;
	UPCGData* MutableData = const_cast<UPCGData*>(InData);
	DataDependencies.Add(MutableData, &bAlreadyInSet);
	// UPROPERTY should be enough, Rooting is overkill
	//if (!bAlreadyInSet) { MutableData->AddToRoot(); }
}

void UPCGExFactoryData::BeginDestroy()
{
	// UPROPERTY Should be enough, Rooting is overkill
	//for (UPCGData* DataDependency : DataDependencies) { DataDependency->RemoveFromRoot(); }
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
