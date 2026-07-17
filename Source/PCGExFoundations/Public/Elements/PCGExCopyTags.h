// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"

#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExMatchingDetails.h"

#include "PCGExCopyTags.generated.h"

class UPCGData;

namespace PCGExData
{
	class FTags;
}

namespace PCGExMatching
{
	class FDataMatcher;
}

/**
 * Forward-only utility: copy missing tags from matched Targets onto Sources.
 * Sources are forwarded untouched apart from the added tags (no data duplication).
 * Pairing is driven by the data matching rules; with matching disabled, every
 * Source pairs with every Target.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/copy-tags"))
class UPCGExCopyTagsSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(CopyTags, "Copy Tags", "Copy missing tags from matched Targets onto Sources. Sources are forwarded untouched apart from the added tags.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Matching rules that decide which Targets each Source pairs with. When disabled, every Source pairs with every Target. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Default);

	/** If enabled, never transfers the reserved cluster identity tags (PCGEx/Vtx, PCGEx/Edges, PCGEx/Cluster), keeping it safe to run on vtx/edge data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bProtectClusterTags = true;

	/** Restricts which tag names are eligible to transfer. Leave empty (FilterMode=All) to allow every
	 *  tag but the protected ones. The filter applies to PCGEx-prefixed tags too; only the three cluster
	 *  identity tags are force-protected, and only when Protect Cluster Tags is enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails TagFilters;
};

struct FPCGExCopyTagsContext final : FPCGExContext
{
	friend class FPCGExCopyTagsElement;

	bool bProtectClusterTags = true;
	FPCGExNameFiltersDetails TagFilters;

	TSharedPtr<PCGExMatching::FDataMatcher> DataMatcher;

	// Kept alive for the matcher's lifetime -- FPCGExTaggedData holds only a TWeakPtr to these tags.
	TArray<const UPCGData*> TargetData;
	TArray<TSharedPtr<PCGExData::FTags>> TargetTags;
};

class FPCGExCopyTagsElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CopyTags)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
