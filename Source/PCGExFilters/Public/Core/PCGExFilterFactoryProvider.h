// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPointFilter.h"
#include "Factories/PCGExFactoryProvider.h"
#include "UObject/Object.h"

#include "PCGExFilterFactoryProvider.generated.h"

/**
 * Macro to generate the CreateFactory() implementation for a filter provider.
 * Expects the provider class to be named UPCGEx##_FILTERID##FilterProviderSettings
 * and the factory class to be named UPCGEx##_FILTERID##FilterFactory.
 * The provider must have a Config member that gets copied to the factory.
 *
 * Flow: create factory UObject → copy policies → copy Config → Super::CreateFactory
 * → Init (validate settings against data) → on failure, destroy and return nullptr.
 */
#define PCGEX_CREATE_FILTER_FACTORY(_FILTERID)\
UPCGExFactoryData* UPCGEx##_FILTERID##FilterProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{\
	UPCGEx##_FILTERID##FilterFactory* NewFactory = InContext->ManagedObjects->New<UPCGEx##_FILTERID##FilterFactory>();\
	NewFactory->InitializationFailurePolicy = InitializationFailurePolicy;\
	NewFactory->MissingDataPolicy = MissingDataPolicy;\
	NewFactory->Config = Config; Super::CreateFactory(InContext, NewFactory);\
	if(!NewFactory->Init(InContext)){ InContext->ManagedObjects->Destroy(NewFactory); return nullptr; }\
	return NewFactory; }

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class PCGEXFILTERS_API UPCGExFilterProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoFilterPoint)

public:
	UPCGExFilterProviderSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(AbstractFilterFactory, "Filter : Abstract", "Creates an abstract filter definition.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Filter);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override;
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
	virtual bool ShouldCancel(FPCGExFactoryProviderContext* InContext, PCGExFactories::EPreparationResult InResult) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Filter Priority.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;


	/** How to handle failed attribute initialization. Usually, the reason is missing attributes, but can also be unsupported filter type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	EPCGExFilterNoDataFallback InitializationFailurePolicy = EPCGExFilterNoDataFallback::Error;

	/** How to handle missing data. This only applies to filters that rely on local data pins to output meaningful results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(EditCondition="ShowMissingDataPolicy()", PCG_NotOverridable))
	EPCGExFilterNoDataFallback MissingDataPolicy = EPCGExFilterNoDataFallback::Fail;

protected:
#if WITH_EDITOR
	virtual bool ShowMissingDataPolicy_Internal() const
	{
		return false;
	}

	UFUNCTION()
	bool ShowMissingDataPolicy() const
	{
		return ShowMissingDataPolicy_Internal();
	}
#endif
};


UCLASS(Abstract, MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExFilterCollectionProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

protected:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
#endif

	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoFilterCollection)

	virtual FName GetMainOutputPin() const override;
};
