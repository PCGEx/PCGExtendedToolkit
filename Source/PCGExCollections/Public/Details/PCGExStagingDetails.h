// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMath.h"
#include "PCGExStagingDetails.generated.h"

class UPCGExBitmaskCollection;
class UPCGParamData;

/**
 * Controls how a numeric index maps to collection entries. Used when Distribution
 * is set to Index mode. Supports pick mode (ascending/descending/weight-sorted),
 * index safety (clamping/tiling for out-of-range), and optional remap to collection size.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExAssetDistributionIndexDetails
{
	GENERATED_BODY()

	FPCGExAssetDistributionIndexDetails();

	/** Index picking mode*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexPickMode PickMode = EPCGExIndexPickMode::Ascending;

	/** Index sanitization behavior */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Tile;

	/** The name of the attribute index to read index selection from.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector IndexSource;

	PCGEX_SETTING_VALUE_DECL(Index, double);

	/** Whether to remap index input value to collection size */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bRemapIndexToCollectionSize = false;

	/** Whether to remap index input value to collection size */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExTruncateMode TruncateRemap = EPCGExTruncateMode::None;
};

USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExComponentTaggingDetails
{
	GENERATED_BODY()

	FPCGExComponentTaggingDetails()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bForwardInputDataTags = true;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	//bool bOutputTagsToAttributes = false;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	//bool bAddTagsToData = false;
};

USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExAssetTaggingDetails : public FPCGExComponentTaggingDetails
{
	GENERATED_BODY()

	FPCGExAssetTaggingDetails() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExCollections.EPCGExAssetTagInheritance"))
	uint8 GrabTags = static_cast<uint8>(EPCGExAssetTagInheritance::Asset);

	bool IsEnabled() const { return GrabTags != 0; }
};

/**
 * Main distribution configuration. Determines how points select collection entries:
 * - Index: map a per-point attribute to an entry index
 * - Random: uniform random based on seed
 * - WeightedRandom: probability proportional to entry Weight
 * Also supports optional category-based filtering (restrict picks to a named category).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExAssetDistributionDetails
{
	GENERATED_BODY()

	FPCGExAssetDistributionDetails() = default;

	/** If enabled, will limit pick to entries flagged with a specific category. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Category", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseCategories = false;

	/** Category name source (constant or per-point attribute). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Category", meta=(PCG_Overridable, EditCondition="bUseCategories"))
	FPCGExInputShorthandNameName Category = FPCGExInputShorthandNameName(FName("Category"), FName("MyCategory"), false);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExSeedComponents"))
	uint8 SeedComponents = 0;

	/** Distribution type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistribution Distribution = EPCGExDistribution::WeightedRandom;

	/** Index settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Index Settings", EditCondition="Distribution == EPCGExDistribution::Index"))
	FPCGExAssetDistributionIndexDetails IndexSettings;

	/** Note that this is only accounted for if selected in the seed component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 LocalSeed = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExMicroCacheDistributionDetails
{
	GENERATED_BODY()

	FPCGExMicroCacheDistributionDetails() = default;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExSeedComponents"))
	uint8 SeedComponents = 0;

	/** Distribution type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDistribution Distribution = EPCGExDistribution::WeightedRandom;

	/** Index settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Index Settings", EditCondition="Distribution == EPCGExDistribution::Index"))
	FPCGExAssetDistributionIndexDetails IndexSettings;

	/** Note that this is only accounted for if selected in the seed component. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 LocalSeed = 0;
};

/**
 * Configures how to build a dynamic collection from a PCG Attribute Set.
 * Maps attribute names to entry properties (asset path, weight, category),
 * allowing collections to be authored as data rather than UDataAssets.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExAssetAttributeSetDetails
{
	GENERATED_BODY()

	FPCGExAssetAttributeSetDetails() = default;

	/** Name of the attribute on the AttributeSet that contains the asset path to be staged */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName AssetPathSourceAttribute = FName("AssetPath");

	/** Name of the attribute on the AttributeSet that contains the asset weight, if any. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName WeightSourceAttribute = NAME_None;

	/** Name of the attribute on the AttributeSet that contains the asset category, if any. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName CategorySourceAttribute = NAME_None;
};
