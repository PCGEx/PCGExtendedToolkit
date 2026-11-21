// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "PCGExFiltersSubSystem.generated.h"

#define PCGEX_FILTERS_SUBSYSTEM UPCGExFiltersSubSystem* PCGExFiltersSubsystem = UPCGExFiltersSubSystem::GetSubsystemForCurrentWorld(); check(PCGExFiltersSubsystem)

class UPCGExConstantFilterFactory;

namespace PCGExPointFilter
{
	class IFilter;
}

UCLASS()
class PCGEXFILTERS_API UPCGExFiltersSubSystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPCGExFiltersSubSystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** To be used when a PCG component can not have a world anymore, to unregister itself. */
	static UPCGExFiltersSubSystem* GetSubsystemForCurrentWorld();

	/** Will return the subsystem from the World if it exists and if it is initialized */
	static UPCGExFiltersSubSystem* GetInstance(UWorld* World);

	TSharedPtr<PCGExPointFilter::IFilter> GetConstantFilter(const bool bValue) const;

protected:
	UPROPERTY()
	TObjectPtr<UPCGExConstantFilterFactory> ConstantFilterFactory_TRUE;

	UPROPERTY()
	TObjectPtr<UPCGExConstantFilterFactory> ConstantFilterFactory_FALSE;
};
