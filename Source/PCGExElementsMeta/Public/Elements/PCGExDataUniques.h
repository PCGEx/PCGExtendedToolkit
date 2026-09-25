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

#include "PCGExDataUniques.generated.h"

UENUM()
enum class EPCGExDataUniquesOutputMode : uint8
{
	Merged = 0 UMETA(DisplayName = "Merged", ToolTip="A single attribute set with one row per unique combination."),
	PerRow = 1 UMETA(DisplayName = "Per Row", ToolTip="One attribute set per unique combination, each holding a single row."),
};

namespace PCGExDataUniques
{
	inline const FName OutputUniquesLabel = FName("Uniques");
	inline const FName OutputDiscardedLabel = FName("Discarded");
}

/**
 * Forward-only @Data triage: finds the unique combinations of one or more @Data attributes across every
 * input, emits the full @Data row of the first input seen for each combination as an attribute set, and
 * tags each forwarded input with the identifier of the row it belongs to.
 *
 * Uniqueness is decided on a 63-bit hash of the key values (type included, PCGExHashHelpers) that is the same on
 * every session and platform, so an identifier can be saved and compared later. It is never re-checked for
 * equality, so two colliding combinations would share a row. String and Name keys compare case-insensitively
 * (ASCII), like FString/FName equality. Inputs are never duplicated -- they are forwarded as-is, keeping their
 * tags and gaining one when identification is enabled.
 *
 * Typical use: Get Properties Data (@Data mode) -> Data Uniques -> loop over the Uniques rows and process,
 * for each row, only the inputs carrying the matching identifier tag.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc",
	meta=(Keywords = "unique distinct data domain triage group dedupe", PCGExNodeLibraryDoc="metadata/analyze/data-uniques"))
class UPCGExDataUniquesSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(DataUniques, "Data Uniques", "Collect the unique combinations of @Data attribute values across inputs as attribute set rows, and tag each input with the identifier of the row it matches.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	// Forwarded pins mirror the input shape; the Uniques pin is pinned to Param via GetCurrentPinTypesID.
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

protected:
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** @Data attributes whose combined values define uniqueness, in order. The "@Data." prefix is optional; the domain is always @Data.
	 *  Plain attribute names only -- sub-selections (".X", ".Length") and properties ($...) are rejected.
	 *  Inputs missing any of these (or carrying a container/extended type) are routed to the Discarded pin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Keys", meta=(PCG_Overridable))
	TArray<FName> KeyAttributes;

	/** Comma-separated key attribute names, appended to the list above. Handy for overrides. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Keys", meta=(PCG_Overridable))
	FString CommaSeparatedKeyAttributes;

	/** One merged attribute set (one row per unique combination), or one attribute set per unique combination.
	 *  Merged: a row whose representative input lacks an attribute another row has reads that type's default (zero/empty);
	 *  a name seen with two different types keeps the first type, and the other values are skipped with a warning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_NotOverridable))
	EPCGExDataUniquesOutputMode OutputMode = EPCGExDataUniquesOutputMode::Merged;

	/** Which @Data attributes of the representative input (first seen per combination) are copied into its row.
	 *  Leave on All to copy every @Data attribute. The filter applies to PCGEx-prefixed attributes too.
	 *  A source attribute named like the identifier is never copied while Write Identifier is on. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FPCGExNameFiltersDetails RowAttributes;

	/** Write the row identifier (a hash of the key values, the same on every session so it can be saved) as an extra
	 *  column on each row AND as a tag on each forwarded input, so rows and inputs can be paired downstream. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta=(PCG_Overridable))
	bool bWriteIdentifier = true;

	/** Name of the int64 identifier column on the rows; also the tag prefix on the inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta=(PCG_Overridable, EditCondition="bWriteIdentifier"))
	FName IdentifierAttributeName = FName("UniqueId");

	/** If enabled, inputs are tagged "IdentifierAttributeName:Value", replacing any same-prefixed tag they already carry.
	 *  Otherwise the bare value is used as the tag, and bare tags from an upstream Data Uniques are kept alongside it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta=(PCG_Overridable, EditCondition="bWriteIdentifier"))
	bool bPrefixTagWithAttributeName = true;
};

struct FPCGExDataUniquesContext final : FPCGExContext
{
	friend class FPCGExDataUniquesElement;

	// Resolved in Boot: every key, @Data domain, deduplicated, in user order (order matters for the hash).
	TArray<FPCGAttributeIdentifier> KeyIdentifiers;

	// Initialized copy of the settings' filter (Init() parses the comma-separated names).
	FPCGExNameFiltersDetails RowAttributes;

	bool bWriteIdentifier = true;
	FName IdentifierName = NAME_None;
};

class FPCGExDataUniquesElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(DataUniques)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
