// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExFactoryData.h"

#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExStreamingHelpers.h"
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

void UPCGExFactoryData::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const
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
}

void UPCGExFactoryData::BeginDestroy()
{
	PCGExHelpers::SafeReleaseHandle(AssetsDependencies);
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
