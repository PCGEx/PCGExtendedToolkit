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
	AttributeSet = 1 UMETA(Hidden, DisplayName = "Attribute Set", Tooltip="Use a single attribute set that will be converted to a dynamic collection on the fly"),
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

/** Selects between inline (Legacy) distribution settings or an external factory provided on the Selector input pin. */
UENUM()
enum class EPCGExSelectorMode : uint8
{
	Legacy   = 0 UMETA(DisplayName = "Legacy", ToolTip="Use the inline distribution settings configured on this node."),
	External = 1 UMETA(DisplayName = "External (Factory)", ToolTip="Use a selector factory provided via the Selector input pin."),
	Unset    = 8 UMETA(Hidden),
};

/** Behavior when a point's Category attribute does not match any named category in the collection. */
UENUM()
enum class EPCGExMissingCategoryBehavior : uint8
{
	Skip    = 0 UMETA(DisplayName = "Skip", ToolTip="Skip the point -- no entry is picked."),
	UseMain = 1 UMETA(DisplayName = "Use Main", ToolTip="Fall back to picking from the collection's main pool."),
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

// None is declared first (with an explicit value) so it leads the details dropdown as the default.
/** Whether an entry uses its own settings (Local), the collection's global settings (Global), or none at all (None). */
UENUM(BlueprintType)
enum class EPCGExEntryVariationMode : uint8
{
	Local  = 0 UMETA(DisplayName = "Local", ToolTip="This entry defines its own settings. This can be overruled in the collection settings.", ActionIcon="EntryRule"),
	Global = 1 UMETA(DisplayName = "Global", ToolTip="Uses collections settings", ActionIcon="CollectionRule"),
	None   = 2 UMETA(DisplayName = "None", ToolTip="No local data. Variations resolve to identity; fitting overrides resolve to the consuming node's settings.", ActionIcon="Disabled"),
};

/** Collection-level override rule: let entries choose (PerEntry) or force global (Overrule). */
UENUM(BlueprintType)
enum class EPCGExGlobalVariationRule : uint8
{
	PerEntry = 0 UMETA(DisplayName = "Per Entry", ToolTip="Let the entry choose whether it's using collection settings or its own", ActionIcon="EntryRule"),
	Overrule = 1 UMETA(DisplayName = "Overrule", ToolTip="Disregard the entry settings and enforce collection settings", ActionIcon="CollectionRule")
};

/**
 * How a subcollection entry's aggregate Staging.Bounds is computed from its children.
 * Consumed by selectors that reason about entry extents (e.g. Best Fit).
 * Extents-only: aggregate bounds are centered at origin; center offsets are not aggregated.
 */
UENUM(BlueprintType)
enum class EPCGExSubcollectionBoundsMode : uint8
{
	UnionAABB    = 0 UMETA(DisplayName = "Union AABB", ToolTip="Enclosing AABB over all child bounds. Preserves worst-case footprint."),
	MeanExtents  = 1 UMETA(DisplayName = "Mean Extents", ToolTip="Axis-wise average of child extents. More representative of typical entry size."),
	WeightedMean = 2 UMETA(DisplayName = "Weight-Weighted Mean", ToolTip="Extents weighted by Entry.Weight. Biases toward likely-picked children."),
	MaxExtents   = 3 UMETA(DisplayName = "Max Extents", ToolTip="Axis-wise max of child extents. Upper bound of any possible child pick."),
};

namespace PCGExCollections::Labels
{
	const FName SourceAssetCollection = TEXT("AttributeSet");

	const FName SourceCollectionMapLabel = TEXT("Map");
	const FName OutputCollectionMapLabel = TEXT("Map");

	const FName SourceSelectorLabel = TEXT("Selector");
	const FName OutputSelectorLabel = TEXT("Selector");
	const FName SourceSelectorsLabel = TEXT("Selectors");
	const FName SourceNoiseLabel = TEXT("Noise");

	const FName Tag_CollectionPath = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Path"));
	const FName Tag_CollectionIdx = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Idx"));
	const FName Tag_EntryIdx = FName(PCGExCommon::PCGExPrefix + TEXT("CollectionEntry"));

	const FName CollectionMapPin = TEXT("CollectionMap");

	const FName MeshesPin = TEXT("Meshes");
	const FName ActorsPin = TEXT("Actors");
	const FName LevelsPin = TEXT("Levels");
}
