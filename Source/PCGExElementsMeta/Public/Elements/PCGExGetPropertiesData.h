// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGExPropertyWriter.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"

#include "PCGExGetPropertiesData.generated.h"

class UPCGExPropertyCollectionComponent;
class UPCGExPropertySchemaAsset;

UENUM()
enum class EPCGExSchemaPresenceMode : uint8
{
	AllRequired = 0 UMETA(DisplayName = "All Required", Tooltip = "Keep rows whose resolved component imports every one of the required schema assets (recursively)."),
	AnyRequired = 1 UMETA(DisplayName = "Any Required", Tooltip = "Keep rows whose resolved component imports at least one of the required schema assets (recursively)."),
};

/**
 * Reads property values from UPCGExPropertyCollectionComponent instances referenced by soft paths
 * on input points or attribute set rows, then writes the resolved values as per-row attributes
 * on a forwarded copy of the input.
 *
 * No loading -- only tentatively resolves references via FSoftObjectPath::ResolveObject(); rows
 * that point to assets not currently in memory are treated as unresolved.
 *
 * Supports actors (looks up the first UPCGExPropertyCollectionComponent on the actor) and direct
 * component references (cast). Per-component schema resolution is deduplicated -- multiple rows
 * pointing at the same component pay BuildResolvedSchema once.
 *
 * Runs entirely off the game thread.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc",
	meta=(Keywords = "property component schema actor attribute", PCGExNodeLibraryDoc="metadata/keys/get-properties-data"))
class UPCGExGetPropertiesDataSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetPropertiesData, "Get Properties Data", "Resolve actor/component references and write the property values they expose as per-row attributes.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}

#endif

	// Output pin mirrors the input shape: PCGEX_PIN_ANY on both sides relies on dynamic pin
	// matching so points-in stays points-out and param-in stays param-out.
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Attribute on each input row holding the soft object path of the actor (or the
	 *  UPCGExPropertyCollectionComponent directly). FString-typed attributes fall back to
	 *  parsing the string as an FSoftObjectPath. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Source", meta=(PCG_Overridable))
	FName SourceAttribute = FName(TEXT("ActorReference"));

	/** How to pick which properties to write per row. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Source", meta=(PCG_NotOverridable))
	EPCGExPropertyOutputMode OutputMode = EPCGExPropertyOutputMode::AllFound;

	/** Explicit list of properties to output. The Configs entries take precedence; IncludedSchemas
	 *  contribute any property name they declare. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Source",
		meta=(PCG_Overridable, EditCondition="OutputMode == EPCGExPropertyOutputMode::Explicit", EditConditionHides, ShowOnlyInnerProperties))
	FPCGExPropertyOutputSettings PropertyOutputSettings;

	/** Emit the "Map" attribute set describing external resources referenced by written properties. Always emitted in AllFound mode (the toggle is hidden there). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Source", AdvancedDisplay,
		meta=(DisplayName="Output Map", EditCondition="OutputMode == EPCGExPropertyOutputMode::Explicit", EditConditionHides))
	bool bOutputMap = false;

	/** Map sidecar is emitted in AllFound mode or when the node-level toggle is on. */
	bool WantsOutputMap() const
	{
		return OutputMode == EPCGExPropertyOutputMode::AllFound || bOutputMap;
	}

	/** Forward each input's tags to the corresponding output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Source")
	bool bForwardInputTags = true;

	/** Drop rows whose soft path didn't resolve to a UPCGExPropertyCollectionComponent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering")
	bool bOmitUnresolvedEntries = true;

	/** Drop rows where one or more requested properties could not be located on the resolved
	 *  component. Has no effect when OutputMode is AllFound. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering",
		meta=(EditCondition="OutputMode == EPCGExPropertyOutputMode::Explicit", EditConditionHides))
	bool bOmitPartialMatches = false;

	/** When non-empty, only keep rows whose resolved component imports the listed schema asset(s).
	 *  The check walks the component's full import tree recursively. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering")
	TArray<TObjectPtr<UPCGExPropertySchemaAsset>> RequiredSchemas;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering")
	EPCGExSchemaPresenceMode SchemaPresenceMode = EPCGExSchemaPresenceMode::AllRequired;
};

namespace PCGExGetPropertiesData
{
	inline const FName SourcesPin = TEXT("Sources");

	/** One slot per input row. Path is the parsed FSoftObjectPath; Component is set by the parallel
	 *  resolve phase. Slots are stored input-major + row-minor (same scheme as GetCollectionData's
	 *  FromInputs path) so per-input writes can walk a contiguous range. */
	struct FSlot
	{
		FSoftObjectPath Path;
		int32 SourceInputIndex = INDEX_NONE;
		int32 RowIndex = INDEX_NONE;
		UPCGExPropertyCollectionComponent* Component = nullptr;
		// Set by the schema-presence filter (Phase 3b) for rows whose component would otherwise be
		// non-null but doesn't satisfy RequiredSchemas. Kept separate from Component=null so the
		// row-filter step at the end can treat "did not satisfy schema filter" as a hard drop
		// regardless of bOmitUnresolvedEntries.
		bool bSchemaFiltered = false;
	};

	/** Outcome of writing a single row. Tracked per slot during Phase 5 and consumed by the
	 *  row-filter step.
	 *
	 *  Ordered least-successful to most-successful for readability. The per-row loop initializes
	 *  every row to Complete and only ever downgrades to Partial when an expected property is
	 *  missing on the resolved component; Unresolved and FailedSchema are set up-front based on
	 *  slot state and never transition. The filter step branches on equality, not numeric ordering. */
	enum class ESlotOutcome : uint8
	{
		Unresolved = 0,
		// Path didn't resolve to a component.
		FailedSchema,
		// Component resolved but failed the RequiredSchemas filter -- always dropped when that filter is on.
		Partial,
		// Component resolved but at least one requested property was missing.
		Complete,
		// Component resolved and every requested property was written.
	};
}

struct FPCGExGetPropertiesDataContext final : FPCGExContext
{
	friend class FPCGExGetPropertiesDataElement;

	TArray<PCGExGetPropertiesData::FSlot> Slots;

	// Unique components found in Phase 2, with their resolved schema cached in Phase 3. Parallel
	// arrays -- ComponentToIdx maps from component pointer to index into UniqueComponents /
	// SchemaPerComponent / PropertyLookupPerComponent. Component pointers that fail the
	// schema-presence filter are nulled out in Phase 3b so per-row dispatch can treat them as
	// unresolved.
	TArray<UPCGExPropertyCollectionComponent*> UniqueComponents;
	TArray<TArray<FInstancedStruct>> SchemaPerComponent;
	TMap<UPCGExPropertyCollectionComponent*, int32> ComponentToIdx;

	// Per-component name -> property pointer for O(1) per-row source lookup during writes.
	// Values point into SchemaPerComponent[i] entries -- stable for the AdvanceWork lifetime
	// because SchemaPerComponent is sized once in Phase 3 and never reallocated afterward, and
	// each inner TArray<FInstancedStruct> is assigned exactly once. Built in Phase 3c.
	TArray<TMap<FName, const FInstancedStruct*>> PropertyLookupPerComponent;

	// Global first-hit prototype map: property name -> the FInstancedStruct* of the first
	// surviving component (input-major encounter order) that declares it. Drives output-attribute
	// type + default discovery and AllFound mode's union enumeration. Built in Phase 3c.
	TMap<FName, const FInstancedStruct*> PrototypeByName;

	// Pre-resolved Required schema set -- TSet for O(1) membership check during the recursive
	// import-tree walk in Phase 3b.
	TSet<const UPCGExPropertySchemaAsset*> RequiredSchemaSet;

	// Sources pin inputs, captured in Boot so slot/schema resolution (phases 1-3c) can run there and
	// expose per-property output dependencies to RegisterAssetDependencies before the write.
	TArray<FPCGTaggedData> Inputs;

	/** Registers FPCGExProperty::GatherOutputDependencies across every resolved component schema. */
	virtual void RegisterAssetDependencies() override;
};

class FPCGExGetPropertiesDataElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetPropertiesData)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
