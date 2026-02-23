// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGSettings.h"
#include "UObject/Object.h"

#include "Factories/PCGExFactories.h"
#include "Data/PCGExPointData.h"

#include "PCGExFactoryData.generated.h"

#define PCGEX_FACTORY_NEW_OPERATION(_TYPE) TSharedPtr<FPCGEx##_TYPE> NewOperation = MakeShared<FPCGEx##_TYPE>();

///

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;
}

struct FPCGExFactoryProviderContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Factory"))
struct FPCGExFactoryDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCORE_API)
};

/**
 * 
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCORE_API UPCGExParamDataBase : public UPCGExPointData
{
	GENERATED_BODY()

public:
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Param; } //PointOrParam would be best but it's gray and I don't like it

	virtual void OutputConfigToMetadata();
};

/**
 * 
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCORE_API UPCGExFactoryData : public UPCGExParamDataBase
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExFactoryDataTypeInfo)

	UPROPERTY()
	int32 Priority = 0;

	UPROPERTY()
	bool bCleanupConsumableAttributes = false;

	PCGExFactories::EPreparationResult PrepResult = PCGExFactories::EPreparationResult::None;

	virtual PCGExFactories::EType GetFactoryType() const { return PCGExFactories::EType::None; }

	virtual bool RegisterConsumableAttributes(FPCGExContext* InContext) const;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const;
	virtual void RegisterAssetDependencies(FPCGExContext* InContext) const;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;

	virtual bool WantsPreparation(FPCGExContext* InContext) { return false; }
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) { return PCGExFactories::EPreparationResult::Success; }

	virtual void AddDataDependency(const UPCGData* InData);
	virtual void BeginDestroy() override;
	
protected:
	UPROPERTY()
	TSet<TObjectPtr<UPCGData>> DataDependencies;
};
