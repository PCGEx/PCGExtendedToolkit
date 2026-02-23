// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"

class UPCGParamData;
class UPCGExAssetCollection;

/** Where a node gets its collection from: direct asset ref, runtime attribute set, or path attribute. */
UENUM()
enum class EPCGExCollectionSource : uint8
{
	Asset        = 0 UMETA(DisplayName = "Asset", Tooltip="Use a single collection reference"),
	AttributeSet = 1 UMETA(DisplayName = "Attribute Set", Tooltip="Use a single attribute set that will be converted to a dynamic collection on the fly"),
	Attribute    = 2 UMETA(DisplayName = "Path Attribute", Tooltip="Use an attribute that's a path reference to an asset collection"),
};

/** How indices map to the weight-sorted entry array in FCategory. */
UENUM()
enum class EPCGExIndexPickMode : uint8
{
	Ascending        = 0 UMETA(DisplayName = "Collection order (Ascending)", Tooltip="..."),
	Descending       = 1 UMETA(DisplayName = "Collection order (Descending)", Tooltip="..."),
	WeightAscending  = 2 UMETA(DisplayName = "Weight (Descending)", Tooltip="..."),
	WeightDescending = 3 UMETA(DisplayName = "Weight (Ascending)", Tooltip="..."),
};

/** Top-level distribution strategy: deterministic index, uniform random, or weighted random. */
UENUM()
enum class EPCGExDistribution : uint8
{
	Index          = 0 UMETA(DisplayName = "Index", ToolTip="Pick entries by index, with optional remapping and safety modes"),
	Random         = 1 UMETA(DisplayName = "Random", ToolTip="Uniform random selection (equal probability for all entries)"),
	WeightedRandom = 2 UMETA(DisplayName = "Weighted random", ToolTip="Random selection weighted by entry Weight property"),
};

UENUM()
enum class EPCGExWeightOutputMode : uint8
{
	NoOutput                    = 0 UMETA(DisplayName = "No Output", ToolTip="Don't output weight as an attribute"),
	Raw                         = 1 UMETA(DisplayName = "Raw", ToolTip="Raw integer"),
	Normalized                  = 2 UMETA(DisplayName = "Normalized", ToolTip="Normalized weight value (Weight / WeightSum)"),
	NormalizedInverted          = 3 UMETA(DisplayName = "Normalized (Inverted)", ToolTip="One Minus normalized weight value (1 - (Weight / WeightSum))"),
	NormalizedToDensity         = 4 UMETA(DisplayName = "Normalized to Density", ToolTip="Normalized weight value (Weight / WeightSum)"),
	NormalizedInvertedToDensity = 5 UMETA(DisplayName = "Normalized (Inverted) to Density", ToolTip="One Minus normalized weight value (1 - (Weight / WeightSum))"),
};

UENUM()
enum class EPCGExAssetTagInheritance : uint8
{
	None           = 0,
	Asset          = 1 << 1 UMETA(DisplayName = "Asset"),
	Hierarchy      = 1 << 2 UMETA(Hidden, DisplayName = "Hierarchy"),
	Collection     = 1 << 3 UMETA(DisplayName = "Collection"),
	RootCollection = 1 << 4 UMETA(Hidden, DisplayName = "Root Collection"),
	RootAsset      = 1 << 5 UMETA(Hidden, DisplayName = "Root Asset"),
};

ENUM_CLASS_FLAGS(EPCGExAssetTagInheritance)
using EPCGExAssetTagInheritanceBitmask = TEnumAsByte<EPCGExAssetTagInheritance>;

/** Whether an entry uses its own settings (Local) or the collection's global settings. */
UENUM()
enum class EPCGExEntryVariationMode : uint8
{
	Local  = 0 UMETA(DisplayName = "Local", ToolTip="This entry defines its own settings. This can be overruled in the collection settings.", ActionIcon="EntryRule"),
	Global = 1 UMETA(DisplayName = "Global", ToolTip="Uses collections settings", ActionIcon="CollectionRule")
};

/** Collection-level override rule: let entries choose (PerEntry) or force global (Overrule). */
UENUM()
enum class EPCGExGlobalVariationRule : uint8
{
	PerEntry = 0 UMETA(DisplayName = "Per Entry", ToolTip="Let the entry choose whether it's using collection settings or its own", ActionIcon="EntryRule"),
	Overrule = 1 UMETA(DisplayName = "Overrule", ToolTip="Disregard the entry settings and enforce collection settings", ActionIcon="CollectionRule")
};

namespace PCGExCollections::Labels
{
	const FName SourceAssetCollection = TEXT("AttributeSet");

	const FName SourceCollectionMapLabel = TEXT("Map");
	const FName OutputCollectionMapLabel = TEXT("Map");

	const FName Tag_CollectionPath = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Path"));
	const FName Tag_CollectionIdx = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Idx"));
	const FName Tag_EntryIdx = FName(PCGExCommon::PCGExPrefix + TEXT("CollectionEntry"));
}
