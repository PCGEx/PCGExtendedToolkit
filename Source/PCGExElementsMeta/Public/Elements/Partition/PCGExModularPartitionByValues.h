// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPartition.h"
#include "PCGExPartitionByValues.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"
#include "PCGExModularPartitionByValues.generated.h"

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Partition Rule"))
struct FPCGExDataTypeInfoPartitionRule : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXELEMENTSMETA_API)
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExPartitionRule : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoPartitionRule)

	FPCGExPartitonRuleConfig Config;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="utilities/partitioning/partition-by-values"))
class UPCGExPartitionRuleProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoPartitionRule)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(PartitionRuleFactory, "Partition Rule", "Creates an single partition rule to be used with the Partition by Values node.", FName(GetDisplayName()))

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(PartitionRule);
	}
#endif
	//~End UPCGSettings

	//~Begin UPCGExFactoryProviderSettings
	virtual FName GetMainOutputPin() const override
	{
		return FName("PartitionRule");
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
	//~End UPCGExFactoryProviderSettings

	/** Rule Config */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPartitonRuleConfig Config;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="misc/partition-by-values/partition-rule"))
class UPCGExModularPartitionByValuesSettings : public UPCGExPartitionByValuesBaseSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ModularPartitionByValues, "Partition by Values", "Outputs separate buckets of points based on an attribute' value. Each bucket is named after a unique attribute value. Note that it is recommended to use a Merge before.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscAdd);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	virtual bool GetPartitionRules(FPCGExContext* InContext, TArray<FPCGExPartitonRuleConfig>& OutRules) const override;
};
