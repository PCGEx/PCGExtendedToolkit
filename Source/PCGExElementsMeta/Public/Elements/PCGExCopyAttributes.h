// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Elements/PCGCopyAttributes.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExCopyAttributes.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/modify/copy-attributes"))
class UPCGExCopyAttributesSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	UPCGExCopyAttributesSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(CopyAttributes, "Copy Attributes", "Copy attributes from Source data onto Target data, with full single-value (@Data) to per-element promotion support.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual FString GetAdditionalTitleInformation() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGCopyAttributesOperation Operation = EPCGCopyAttributesOperation::CopyEachSourceToEachTargetRespectively;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bCopyAllAttributes = false;

	/** If checked, it is copying all attributes from all domains, as long as the source domain is supported on the target data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition="bCopyAllAttributes", PCG_Overridable))
	bool bCopyAllDomains = false;

	/** When copying all attributes, a mapping can be specified. If it is empty, it's going to be Default -> Default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition="bCopyAllAttributes && !bCopyAllDomains", EditConditionHides))
	TMap<FName, FName> MetadataDomainsMapping;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition="!bCopyAllAttributes", PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "!bCopyAllAttributes", PCG_Overridable))
	FPCGAttributePropertyOutputSelector OutputTarget;

	/** When a single-value source (@Data) is copied onto a multi-entry target domain, write a concrete metadata entry for every element. When disabled, the value is only written as the (re)created attribute's default value -- elements inherit it without any materialized entries. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMaterializeDataValues = false;
};

struct FPCGExCopyAttributesContext final : FPCGExContext
{
	friend class FPCGExCopyAttributesElement;
};

class FPCGExCopyAttributesElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CopyAttributes)
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
