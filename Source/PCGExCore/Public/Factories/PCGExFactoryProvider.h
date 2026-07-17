// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"

#include "PCGSettings.h"
#include "UObject/Object.h"

#include "PCGExCoreSettingsCache.h" // Boilerplate
#include "PCGExVersion.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Factories/PCGExFactories.h"

#include "PCGExFactoryProvider.generated.h"

#define PCGEX_FACTORY_NAME_PRIORITY FName( Priority != 0 ? FString::Printf(TEXT("[%d] "), Priority) + (bCleanupConsumableAttributes ? FString(TEXT("🆑 ")) : FString(TEXT(""))) + GetDisplayName() : GetDisplayName())
#define PCGEX_FACTORY_TYPE_ID(_TYPE) virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const{ return _TYPE::AsId(); }

///

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;
}

struct FPCGExFactoryProviderContext;

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class PCGEXCORE_API UPCGExFactoryProviderSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExFactoryProviderElement;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const;

public:
	
	UPCGExFactoryProviderSettings(const FObjectInitializer& ObjectInitializer);
	
	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	//PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FactoryProvider, "Factory : Provider", "Creates an abstract factory provider.", FName(GetDisplayName()))
	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Debug);
	}
#endif

	virtual int32 GetDefaultPriority() const
	{
		return 0;
	}

protected:
	UPROPERTY()
	int32 InternalCacheInvalidator = 0;

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExFactoryProviderSettings
public:
	virtual FName GetMainOutputPin() const
	{
		return TEXT("");
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory = nullptr) const;

	virtual bool ShouldCancel(FPCGExFactoryProviderContext* InContext, PCGExFactories::EPreparationResult InResult) const
	{
		return true;
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const;
#endif
	//~End UPCGExFactoryProviderSettings
};

struct PCGEXCORE_API FPCGExFactoryProviderContext : FPCGExContext
{
	friend class FPCGExFactoryProviderElement;
	UPCGExFactoryData* OutFactory = nullptr;
};

class PCGEXCORE_API FPCGExFactoryProviderElement final : public IPCGExElement
{
public:
#if WITH_EDITOR
	virtual bool ShouldLog() const override
	{
		return false;
	}
#endif

protected:
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

public:
	PCGEX_ELEMENT_CREATE_CONTEXT(FactoryProvider)

	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return true;
	}

	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
