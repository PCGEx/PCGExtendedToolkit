// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "UObject/Object.h"

#include "PCGExClusterSketchData.generated.h"

/**
 * One shared value set: overrides layered over the owning layer's schema defaults. Many items may
 * reference the same record; that sharing is what makes an edge split lossless.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchDataRecord
{
	GENERATED_BODY()

	/** Stable identity, minted by the authoring panel and never reused. Bare UPROPERTY on purpose: even
	 *  VisibleAnywhere renders FGuidStructCustomization's editable text box. */
	UPROPERTY()
	FGuid Id;

	/** What the record picker shows. Renaming is free -- Id is the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName Label = NAME_None;

	/** Held, not inherited: FPCGExPropertyOverridesCustomization is a property-type customization, so a
	 *  record shown as the ROOT of a details view renders its overrides as a raw Add/Delete array -- on an
	 *  array that must stay parallel to the schema. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExPropertyOverrides Values;
};

/**
 * One authored domain: what fields exist (Schema) and the sparse set of value sets items may point at.
 * An item whose DataId is invalid stores nothing and resolves every field to the schema default.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchDataLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExPropertySchemaCollection Schema;

	/** Hidden by design: records are addressed by Id, never by array position, and an exposed array
	 *  would let the details panel's row-duplicate mint a second record carrying the same Id. */
	UPROPERTY()
	TArray<FPCGExSketchDataRecord> Records;

	const FPCGExSketchDataRecord* FindRecord(const FGuid& InId) const;
	FPCGExSketchDataRecord* FindRecordMutable(const FGuid& InId);

	/**
	 * Effective property for InName given an ALREADY-RESOLVED record (null = no record), so a caller
	 * holding an Id->index map resolves once instead of rescanning per field.
	 *
	 * NEVER returns null for a name the schema declares: invalid, unresolved and override-less
	 * references all fall through to the schema's own value, because a null makes
	 * FPCGExPropertyWriter::WriteProperties silently republish the PREVIOUS row's value.
	 *
	 * const and mutation-free: called from the PARALLEL edge pre-compile hook.
	 */
	const FInstancedStruct* ResolveEffectiveFrom(const FPCGExSketchDataRecord* InRecord, FName InName) const;

	/** Id -> Records index, built once. The print providers hold one for the whole print. */
	void BuildRecordIndex(TMap<FGuid, int32>& OutIndex) const;

	/** One enabled output config per resolved schema entry, so a writer prints the whole layer dense.
	 *  OutConfigs is Reset before population. */
	void BuildOutputConfigs(TArray<FPCGExPropertyOutputConfig>& OutConfigs) const;

	/** Mints a record and syncs it to the schema in one call, or its panel page renders empty until the
	 *  schema is next edited. EDITOR-ONLY BY CONTRACT: a GUID minted at execute time changes PCG output
	 *  every run and defeats CRC caching. */
	FGuid AddRecord(FName InLabel);

	/** Drop every record no live reference names. @return the number removed. */
	int32 PurgeUnreferenced(TConstArrayView<FGuid> InLiveIds);

	/** Re-mint records whose Id is invalid or duplicates an earlier one (first wins). Both states are
	 *  reachable through load and duplication, and only the first holder of an Id is ever addressable.
	 *  @return the number re-minted. */
	int32 RepairRecordIds();

#if WITH_EDITOR
	/**
	 * Canonicalize schema identity, then bring every record's override array back into parallel
	 * structure with it. Remap runs FIRST: a duplicated schema row aliases two entries in SyncToSchema's
	 * HeaderId index, silently dropping one side's values.
	 *
	 * EDITOR ONLY: FPCGExPropertyOverrides::SyncToSchema's identity matching is itself WITH_EDITOR, so
	 * at runtime it rebuilds every entry from schema defaults. Never call from PostLoad or the print path.
	 */
	void EDITOR_SyncRecordsToSchema();
#endif
};

/**
 * The authored tier of a sketch: what may be annotated, and the sparse annotations themselves.
 *
 * A plain struct. It lives inside FPCGExClusterSketchModel, which on a component lives inside ONE
 * instanced payload object -- that object is duplicated wholesale across reconstruction, so every
 * field in here survives regardless of its specifiers. Nothing here needs CPF_Edit to be preserved.
 *
 * Sketch level carries no records -- there is exactly one sketch, so the schema entries ARE the values.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchData
{
	GENERATED_BODY()

	/** Printed to the @Data domain of the vtx output (and mirrored onto edges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExPropertySchemaCollection SketchProperties;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExSketchDataLayer VertexLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExSketchDataLayer EdgeLayer;

	FPCGExSketchDataLayer& GetLayer(const bool bVertexDomain)
	{
		return bVertexDomain ? VertexLayer : EdgeLayer;
	}

	const FPCGExSketchDataLayer& GetLayer(const bool bVertexDomain) const
	{
		return bVertexDomain ? VertexLayer : EdgeLayer;
	}

	/** Only the first holder of a record id is addressable, so duplicates are resolved before anything
	 *  resolves against them. Safe at runtime -- touches no schema. */
	int32 RepairRecordIds();

#if WITH_EDITOR
	/** Repair ids, then bring both layers' records back into parallel structure with their schemas.
	 *  Reachable from an editor EDIT hook only -- see FPCGExSketchDataLayer::EDITOR_SyncRecordsToSchema. */
	void EDITOR_SyncAll();
#endif
};
