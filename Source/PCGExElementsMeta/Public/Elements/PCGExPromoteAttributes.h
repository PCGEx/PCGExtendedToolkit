// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Data/Utils/PCGExDataForwardDetails.h"

#include "PCGExPromoteAttributes.generated.h"

class UPCGData;
class UPCGExPickerFactoryData;
class UPCGExPropertySchemaAsset;

UENUM()
enum class EPCGExAttributeToTagsAction : uint8
{
	AddTags   = 0 UMETA(DisplayName = "Promote to Tags", ToolTip="Promote element attribute value as data tags"),
	Attribute = 1 UMETA(DisplayName = "Promote to Attribute Set", ToolTip="Output to a new attribute set"),
	Data      = 2 UMETA(DisplayName = "Promote to @Data", ToolTip="Promote element attribute values to @Data domain"),
};

UENUM()
enum class EPCGExAttributeToTagsResolution : uint8
{
	Self                   = 0 UMETA(DisplayName = "Self", ToolTip="Matches a single entry to each input collection, from itself"),
	EntryToCollection      = 1 UMETA(DisplayName = "Entry to Collection", ToolTip="Matches a Source entries to each input collection"),
	CollectionToCollection = 2 UMETA(DisplayName = "Collection to Collection", ToolTip="Matches a single entry per source to matching collection (requires the same number of collections in both pins)"),
};

UENUM()
enum class EPCGExCollectionEntrySelection : uint8
{
	FirstIndex  = 0 UMETA(DisplayName = "First Entry", ToolTip="Uses the first entry in the matching collection"),
	LastIndex   = 1 UMETA(DisplayName = "Last Entry", ToolTip="Uses the last entry in the matching collection"),
	RandomIndex = 2 UMETA(DisplayName = "Random Entry", ToolTip="Uses a random entry in the matching collection"),
	Picker      = 3 UMETA(DisplayName = "Picker", ToolTip="Uses pickers to select indices that will be turned into tags"),
	PickerFirst = 4 UMETA(DisplayName = "Picker (First)", ToolTip="Uses the first valid index using pickers"),
	PickerLast  = 5 UMETA(DisplayName = "Picker (Last)", ToolTip="Uses the last valid index using pickers")
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/keys/hoist-attributes"))
class UPCGExAttributesToTagsSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	UPCGExAttributesToTagsSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PromoteAttributes, "Promote Attributes", "Promote element values to tags or data domain");

	virtual TArray<FText> GetNodeTitleAliases() const override;

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}

	virtual bool HasDynamicPins() const override
	{
		return Action != EPCGExAttributeToTagsAction::Attribute;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Action. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExAttributeToTagsAction Action = EPCGExAttributeToTagsAction::AddTags;

	/** Resolution mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExAttributeToTagsResolution Resolution = EPCGExAttributeToTagsResolution::Self;

	/** Selection mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="Resolution != EPCGExAttributeToTagsResolution::EntryToCollection"))
	EPCGExCollectionEntrySelection Selection = EPCGExCollectionEntrySelection::FirstIndex;

	/** If enabled, prefix the attribute value with the attribute name  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bPrefixWithAttributeName = true;

#pragma region DEPRECATED
	// Pre-remapping selector list. Migrated to AttributeMappings in PCGExApplyDeprecation.
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	TArray<FPCGAttributePropertyInputSelector> Attributes_DEPRECATED;

	// Pre-filter selector list. Migrated to AttributeFilter (plain names) and AttributeMappings (the rest) in PCGExApplyDeprecation.
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FString CommaSeparatedAttributeSelectors_DEPRECATED;
#pragma endregion

	/** Attributes promoted by name, matched against each source's own attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Attributes"))
	FPCGExNameFiltersDetails AttributeFilter = FPCGExNameFiltersDetails(true);

	/** Explicit sources for what a name can't express: renames, $properties, sub-fields, domain-qualified names.
	 *  An attribute read here is never promoted again by the filter. An empty target keeps the source's name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(TitleProperty="{Source}"))
	TArray<FPCGExAttributeSourceToTargetDetails> AttributeMappings;

	/** Schema assets whose property names are promoted as explicit sources, whatever the filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<TObjectPtr<UPCGExPropertySchemaAsset>> IncludedSchemas;

	/** Suppress warning when source has more collections than target. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTooManyCollectionsWarning = false;
};

/** What one source promotes: the explicit rows, then the filter's matches on that source, split by read path. */
struct FPCGExAttributesToTagsMappings
{
	/** Read per row through the broadcaster. */
	TArray<FPCGExAttributeSourceToTargetDetails> Element;

	/** @Data sources, read once per source through PCGExData::Helpers::TryReadDataValue (the broadcaster is point-centric). */
	TArray<FPCGExAttributeSourceToTargetDetails> Data;
};

namespace PCGExPromoteAttributes
{
	/** InExplicit as-is, then every attribute of InData that InFilter (already Init) passes and no explicit row reads.
	 *  Only the data's default domain and @Data are matched; any other domain needs an explicit row. */
	PCGEXELEMENTSMETA_API void ResolveMappings(const UPCGData* InData, const FPCGExAttributesToTagsMappings& InExplicit, const FPCGExNameFiltersDetails& InFilter, FPCGExAttributesToTagsMappings& OutMappings);

#if WITH_EDITOR
	/** Deprecation aid: sets InOutFilter to Include and moves every plain attribute name of InOutMappings and
	 *  InCommaSeparatedSelectors into it. Remapped rows and selectors a name can't express stay explicit mappings. */
	PCGEXELEMENTSMETA_API void MigrateToNameFilter(const FString& InCommaSeparatedSelectors, TArray<FPCGExAttributeSourceToTargetDetails>& InOutMappings, FPCGExNameFiltersDetails& InOutFilter);
#endif
}

/** A non-empty input on a pin: the tagged data (copied by value -- self-contained, no lifetime ties to the
 * temporary GetInputsByPin array), its cached row count, and the mappings resolved on it when values are read from it. */
struct FPCGExAttributesToTagsInput
{
	FPCGTaggedData Tagged;
	int32 NumRows = 0;
	FPCGExAttributesToTagsMappings Mappings;
};

struct FPCGExAttributesToTagsContext final : FPCGExContext
{
	friend class FPCGExAttributesToTagsElement;

	TArray<TObjectPtr<const UPCGExPickerFactoryData>> PickerFactories;

	/** All non-empty main inputs (with cached row counts), gathered once in Boot and reused in AdvanceWork. Self resolves their mappings. */
	TArray<FPCGExAttributesToTagsInput> ValidMains;

	/** Cross-collection (EntryToCollection / CollectionToCollection): resolved "Tags Source" data (with cached row counts and mappings) + matching readers, built in Boot. Empty for Self. */
	TArray<FPCGExAttributesToTagsInput> Sources;
	TArray<FPCGExAttributeToTagDetails> Details;
};

class FPCGExAttributesToTagsElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributesToTags)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
