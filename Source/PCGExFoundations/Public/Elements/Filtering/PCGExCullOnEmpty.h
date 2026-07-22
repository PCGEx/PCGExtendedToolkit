// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGPin.h"
#include "Core/PCGExSettings.h"

#include "PCGExCullOnEmpty.generated.h"

namespace PCGExCullOnEmpty
{
	const FName IsEmptyName("IsEmpty");
}

UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "Misc", meta=(PCGExNodeLibraryDoc="utilities/cull-on-empty"))
class UPCGExCullOnEmptySettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(CullOnEmpty, "Cull On Empty", "Deactivates output pin if all inputs are empty or missing.");
	
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterHub);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}
	
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool ShouldDrawNodeCompact() const override;
#endif
	
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
	
	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCheckOnly = false;
	
	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ScriptName="output_is_empty_enabled"))
	bool bOutputIsEmpty = false;
	
	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOutputIsEmpty"))
	FName OutputIsEmpty = PCGExCullOnEmpty::IsEmptyName;

	/** If enabled, inverts the output bool */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOutputIsEmpty"))
	bool bInvert = false;
	
	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

protected:
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGExCullOnEmptyElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
