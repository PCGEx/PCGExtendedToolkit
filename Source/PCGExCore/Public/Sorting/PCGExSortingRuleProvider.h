// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExSortingDetails.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExSortingRuleProvider.generated.h"

namespace PCGExData
{
	class IBufferProxy;
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Sort Rule"))
struct FPCGExDataTypeInfoSortRule : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCORE_API)
};

/**
 * 
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCORE_API UPCGExSortingRule : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoSortRule)

	FPCGExSortRuleConfig Config;

	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class PCGEXCORE_API UPCGExSortingRuleProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoSortRule)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(SortingRuleFactory, "Sorting Rule", "Creates an single sorting rule to be used with the Sort Points node.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(SortRule);
	}
#endif
	//~End UPCGSettings

	//~Begin UPCGExFactoryProviderSettings
	virtual FName GetMainOutputPin() const override
	{
		return FName("SortingRule");
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
	//~End UPCGExFactoryProviderSettings

	/** Filter Priority.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;

	/** Rule Config */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSortRuleConfig Config;
};

#undef PCGEX_UNSUPPORTED_STRING_TYPES
#undef PCGEX_UNSUPPORTED_PATH_TYPES
