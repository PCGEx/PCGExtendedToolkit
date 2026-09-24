// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExSelectorTagFilter.generated.h"

/**
 * Entry-tag predicate applied by a selector after category routing. Each clause is blank
 * (unconstrained) or a tag list; picks are restricted to the entries that satisfy every clause.
 * A constant or @Data value is resolved once per input; a per-point attribute builds one pool per
 * distinct value, which is the expensive path.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorTagFilterDetails
{
	GENERATED_BODY()

	FPCGExSelectorTagFilterDetails() = default;

	/** Entries must carry every one of these tags. Blank = unconstrained. A tag no entry carries makes the filter match nothing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandNameName RequireAll = FPCGExInputShorthandNameName(FName("TagsAll"), NAME_None, false);

	/** Entries must carry at least one of these tags. Blank = unconstrained. Tags no entry carries are ignored. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandNameName RequireAny = FPCGExInputShorthandNameName(FName("TagsAny"), NAME_None, false);

	/** Entries carrying any of these tags are excluded. Blank = unconstrained. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandNameName Exclude = FPCGExInputShorthandNameName(FName("TagsExclude"), NAME_None, false);

	/** Split every clause value on commas into several tags (trimmed, empties dropped) instead of reading it as a single tag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bParseCommaSeparatedLists = false;

	/** Which tags an entry is matched on: its own tags, and/or its sub-collection's tags when the entry is a sub-collection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExCollections.EPCGExAssetTagInheritance"))
	uint8 TagSources = static_cast<uint8>(EPCGExAssetTagInheritance::Asset);

	/** What to do when the filter leaves the routed pool with nothing to pick. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExMissingTagBehavior MissingTagBehavior = EPCGExMissingTagBehavior::Skip;
};
