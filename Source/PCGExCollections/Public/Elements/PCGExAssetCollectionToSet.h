// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "PCGExAssetCollectionToSet.generated.h"

class UPCGExAssetCollection;
struct FPCGExAssetCollectionEntry;

UENUM()
enum class EPCGExSubCollectionToSet : uint8
{
	Ignore             = 0 UMETA(DisplayName = "Ignore", Tooltip="Ignore sub-collections"),
	Expand             = 1 UMETA(DisplayName = "Expand", Tooltip="Expand the entire sub-collection"),
	PickRandom         = 2 UMETA(DisplayName = "Random", Tooltip="Pick one at random"),
	PickRandomWeighted = 3 UMETA(DisplayName = "Random weighted", Tooltip="Pick one at random, weighted"),
	PickFirstItem      = 4 UMETA(DisplayName = "First item", Tooltip="Pick the first item"),
	PickLastItem       = 5 UMETA(DisplayName = "Last item", Tooltip="Pick the last item"),
	Grammar            = 6 UMETA(DisplayName = "Grammar", Tooltip="Defer to each sub-collection entry's own SubGrammarMode (Flatten recurses, Inherit/Override emit one row). Only supported on the Get Collection Data node."),
};

UENUM()
enum class EPCGExWeightNormalization : uint8
{
	None          = 0 UMETA(DisplayName = "None", Tooltip="No normalization. Weight is written as the raw integer."),
	PerCategory   = 1 UMETA(DisplayName = "Per Category", Tooltip="Weight is written as a float, normalized against the sum of weights within each category. Entries with no category share a single bucket."),
	PerCollection = 2 UMETA(DisplayName = "Per Collection", Tooltip="Weight is written as a float, normalized against the sum of weights within each owning collection."),
	Global        = 3 UMETA(DisplayName = "Global", Tooltip="Weight is written as a float, normalized against the sum of all entry weights."),
};

UENUM()
enum class EPCGExCategoryInheritance : uint8
{
	None      = 0 UMETA(DisplayName = "None", Tooltip="No inheritance. Each entry keeps its own category as authored."),
	FillEmpty = 1 UMETA(DisplayName = "Fill Empty", Tooltip="When an entry's category is None, inherit the closest non-None category from the ancestor chain of sub-collection entries."),
	Replace   = 2 UMETA(DisplayName = "Replace", Tooltip="The closest non-None category from the ancestor chain of sub-collection entries always wins. The entry's own category survives only when no ancestor provides one."),
};

UCLASS(Hidden, MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="staging/utilities/asset-collection-to-set"))
class UPCGExAssetCollectionToSetSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExAssetCollectionToSetElement;

	//~Begin UObject interface
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

public:
	UPCGExAssetCollectionToSetSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AssetCollectionToSet, "Asset Collection to Set", "Converts an asset collection to an attribute set.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}

	virtual TOptional<FPCGNodeThumbnailProxy> GetNodeThumbnail() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	/** The asset collection to convert to an attribute set */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TSoftObjectPtr<UPCGExAssetCollection> AssetCollection;

	/** Attribute names */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSubCollectionToSet SubCollectionHandling = EPCGExSubCollectionToSet::PickRandomWeighted;

	/** If enabled, allows duplicate entries (duplicate is same object path & category) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bAllowDuplicates = true;

	/** If enabled, invalid or empty entries are removed from the output */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOmitInvalidAndEmpty = true;

	/** Optionally include/exclude entries by category name. Tested against both leaf entries and
	 *  sub-collection containers; excluding a sub-collection skips its descendants entirely. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNameFiltersDetails CategoryFilters;


	/** Write the asset path to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAssetPath = true;

	UPROPERTY(meta=(PCG_NotOverridable))
	bool bWriteAssetClass = true;

	/** Name of the attribute on the AttributeSet that contains the asset path to be staged */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Asset Path", EditCondition="bWriteAssetPath"))
	FName AssetPathAttributeName = FName("AssetPath");

	UPROPERTY()
	FName AssetClassAttributeName = NAME_None;

	/** Write the asset weight to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteWeight = true;

	/** Name of the attribute on the AttributeSet that contains the asset weight, if any. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Weight", EditCondition="bWriteWeight"))
	FName WeightAttributeName = FName("Weight");

	/** How (and whether) to normalize the weight value. When set to anything other than None, the
	 *  Weight attribute is written as a float in [0..1] instead of the raw int32. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName = " └─ Normalize", EditCondition="bWriteWeight", HideEditConditionToggle))
	EPCGExWeightNormalization WeightNormalization = EPCGExWeightNormalization::None;

	/** Write the asset category to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCategory = false;

	/** Name of the attribute on the AttributeSet that contains the asset category, if any. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Category", EditCondition="bWriteCategory"))
	FName CategoryAttributeName = FName("Category");

	/** How (and whether) to fold parent sub-collection categories down into descendant entries during
	 *  expansion. Also applies to Per Category weight normalization, so the buckets reflect the
	 *  resolved categories the entries are actually written with. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName = " └─ Inheritance", EditCondition="bWriteCategory", HideEditConditionToggle))
	EPCGExCategoryInheritance CategoryInheritance = EPCGExCategoryInheritance::None;

	/** Write the asset bounds extents to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteExtents = false;

	/** Name of the attribute on the AttributeSet that contains the asset bounds' Extents, if any. Otherwise 0 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Extents", EditCondition="bWriteExtents"))
	FName ExtentsAttributeName = FName("Extents");

	/** Write the asset minimum bounds to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMin = false;

	/** Name of the attribute on the AttributeSet that contains the asset BoundsMin, if any. Otherwise 0 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="BoundsMin", EditCondition="bWriteBoundsMin"))
	FName BoundsMinAttributeName = FName("BoundsMin");

	/** Write the asset maximum bounds to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMax = false;

	/** Name of the attribute on the AttributeSet that contains the asset BoundsMax, if any. Otherwise 0 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="BoundsMax", EditCondition="bWriteBoundsMax"))
	FName BoundsMaxAttributeName = FName("BoundsMax");

	/** Write the asset nesting depth to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNestingDepth = false;

	/** Name of the attribute on the AttributeSet that contains the asset depth, if any. Otherwise -1 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Nesting Depth", EditCondition="bWriteNestingDepth"))
	FName NestingDepthAttributeName = FName("NestingDepth");

	/** Additional custom properties to extract from the collection's CollectionProperties (and per-entry overrides). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(ShowOnlyInnerProperties))
	FPCGExPropertyOutputSettings PropertyOutputSettings;
};

class FPCGExAssetCollectionToSetElement final : public IPCGExElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;

	/** Collected entry along with the collection that directly owns it and the resolved
	 *  category to write (after inheritance has been applied during recursion). */
	struct FEntryWithHost
	{
		const FPCGExAssetCollectionEntry* Entry = nullptr;
		const UPCGExAssetCollection* Host = nullptr;
		FName Category = NAME_None;
	};

	/** Invariant inputs to ProcessEntry that don't change across recursion levels.
	 *  Bundled to keep the recursive call site readable and stable as new options are added. */
	struct FProcessEntryContext
	{
		FPCGExContext* Context = nullptr;
		const FPCGExNameFiltersDetails* CategoryFilters = nullptr;
		EPCGExSubCollectionToSet SubHandling = EPCGExSubCollectionToSet::Ignore;
		EPCGExCategoryInheritance CategoryInheritance = EPCGExCategoryInheritance::None;
		bool bOmitInvalidAndEmpty = true;
		bool bNoDuplicates = true;
	};

protected:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	static void ProcessEntry(
		const FProcessEntryContext& Ctx,
		const FPCGExAssetCollectionEntry* InEntry,
		const UPCGExAssetCollection* InHost,
		TArray<FEntryWithHost>& OutEntries,
		const FName EffectiveParentCategory,
		TSet<uint64>& GUIDS);
};
