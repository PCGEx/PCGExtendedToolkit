// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExSettingsDetails.h"
#include "UObject/Object.h"

namespace PCGExData
{
	class FFacadePreloader;
	class FFacade;
	class FPointIO;
}

struct FPCGExContext;

namespace PCGExMT
{
	class FScopedContainer;
	struct FScope;
	class FTaskManager;
}

class FPCGMetadataAttributeBase;
/**
 * 
 */
class PCGEXCORE_API FPCGExOperation : public TSharedFromThis<FPCGExOperation>
{
public:
	FPCGExOperation() = default;
	virtual ~FPCGExOperation() = default;
	void BindContext(FPCGExContext* InContext);

#if WITH_EDITOR
	virtual void UpdateUserFacingInfos();
#endif

	virtual void RegisterAssetDependencies(FPCGExContext* InContext);

	TSharedPtr<PCGExData::FFacade> PrimaryDataFacade;
	TSharedPtr<PCGExData::FFacade> SecondaryDataFacade;

	/** Factory stage of the consumable veto chain: whoever creates this operation from a pin factory copies the factory's toggle here. Node-owned operations keep the default. */
	bool bCleanupConsumableAttributes = true;

	virtual void RegisterConsumableAttributesWithFacade(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade) const;
	virtual void RegisterPrimaryBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) const;

	virtual void InitForScopes(const TArray<PCGExMT::FScope>& Loops);
	virtual TSharedPtr<PCGExMT::FScopedContainer> GetScopedContainer(const PCGExMT::FScope& InScope) const;

protected:
	FPCGExContext* Context = nullptr;

	/** Inits an operand read through a setting value, with bCleanupConsumableAttributes as its factory gate. */
	template <typename T>
	[[nodiscard]] bool InitSettingValue(TSharedPtr<PCGExDetails::TSettingValue<T>>& OutValue, TSharedPtr<PCGExDetails::TSettingValue<T>> InValue, const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped = true, const bool bCaptureMinMax = false) const
	{
		return PCGExDetails::InitSettingValueGated(OutValue, MoveTemp(InValue), bCleanupConsumableAttributes, InDataFacade, bSupportScoped, bCaptureMinMax);
	}

	//~End UPCGExOperation interface
};
