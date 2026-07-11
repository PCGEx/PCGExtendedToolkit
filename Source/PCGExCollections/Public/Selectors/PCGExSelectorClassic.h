// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "Details/PCGExStagingDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"

#include "PCGExSelectorClassic.generated.h"

/**
 * Selector-specific configuration for the Classic selector. Shared verbatim between the
 * palette node settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for
 * serialization). Eliminates the field-by-field copy in CreateFactory.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorClassicConfig
{
	GENERATED_BODY()

	/** Distribution strategy. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistribution Mode = EPCGExDistribution::WeightedRandom;

	/** Index picking configuration. Only used when Mode is Index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Index Settings", EditCondition="Mode == EPCGExDistribution::Index", EditConditionHides))
	FPCGExAssetDistributionIndexDetails IndexConfig;
};

/**
 * Classic (built-in) selector factory data. Supports Index, Random, and WeightedRandom
 * picks via the Mode enum -- consistent with the Legacy inline struct UX on the consuming
 * node so users can swap between inline and factory configuration without re-learning the knobs.
 *
 * User-authored custom factories should subclass UPCGExSelectorFactoryData directly
 * and override CreateEntryOperation; this class is a reference example of that contract.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorClassicFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorClassicConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
};

/**
 * Palette node: "Selector : Classic". Produces the built-in selector factory for the selected Mode.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/selector-classic"))
class PCGEXCOLLECTIONS_API UPCGExSelectorClassicFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorClassic, "Selector : Classic",
		"Built-in selector factory. Supports Index, Random, and Weighted Random selection modes.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorClassicConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
