// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExSchemaMerging.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExActorMeshClassificator.h"
#include "Helpers/PCGExActorSplineClassificator.h"
#include "Helpers/PCGExBoundsEvaluator.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "Helpers/PCGExLevelExportHandler.h"

#include "PCGExDefaultLevelDataExporter.generated.h"

class AActor;
class UPCGBasePointData;
class UPCGExLevelExportHandler;
class UPCGMetadata;
class UStaticMeshComponent;

#if WITH_EDITOR
class FPCGExExportSlotWriter;
namespace PCGExLevelExport
{
	struct FHandlerRegistration;
}
#endif

UENUM()
enum class EPCGExValueTagMode : uint8
{
	NoParsing    UMETA(DisplayName = "No Parsing", ToolTip="Tags are written as-is to the instance tag string; no attribute parsing."),
	Parse        UMETA(DisplayName = "Parse", ToolTip="Plain tags become bool=true attributes; Name:Value tags become typed attributes. The instance tag string is never written in this mode."),
	ParseAndKeep UMETA(DisplayName = "Parse and Keep", ToolTip="Same as Parse, but the name-part of each value tag is also written to the instance tag string."),
};

/**
 * Default level data exporter that replicates the engine's UPCGLevelToAsset behavior.
 *
 * For each qualifying actor of the export source (a whole level, or one actor's subtree):
 * - Classifies actors into Mesh and/or Spline, Actor or Level (or skips them) -- unless their
 *   StaticClassifyMesh / StaticClassifySpline tags classify them outright, or a registered export
 *   handler claims them first (see UPCGExLevelExportHandler)
 * - Hands every candidate (the subtree root included) to every export handler; the built-in Mesh /
 *   Actor / Level / Spline handlers harvest the actors classified into their slot
 * - Emits one point data per slot on that slot's pin, with the actor's transform, bounds, name and
 *   value tags as metadata; splines land on the Splines pin as one spline data each, those values on @Data
 *
 * When bGenerateCollections is enabled, raw metadata is replaced with collection entry hashes
 * (int64 PCGEx/CollectionEntry): per-entry slots (actors) are built into embedded collections here;
 * shared slots (meshes, levels, ...) are handed back as captures for the collection's cross-entry
 * compaction, which writes their hashes and the Collection Map.
 *
 * Skips: hidden actors, editor-only actors, level script actors, info actors, brushes.
 * Supports tag/class include/exclude filtering (same pattern as level collection bounds).
 */
UCLASS(DisplayName = "Default Level Data Exporter", meta=(PCGExNodeLibraryDoc="staging/collections/pcg-data-asset-collection/level-data-exporter-default"))
class PCGEXCOLLECTIONS_API UPCGExDefaultLevelDataExporter : public UPCGExLevelDataExporter
{
	GENERATED_BODY()

public:
	UPCGExDefaultLevelDataExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Actor content filter. Defaults to UPCGExDefaultActorContentFilter. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExActorContentFilter> ContentFilter;

	/** Determines which actors are treated as mesh containers (parsed for static/instanced
	 *  mesh components). Defaults to UPCGExDefaultActorMeshClassificator. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExActorMeshClassificator> MeshClassificator;

	/** Determines which actors are harvested for their splines, on top of or instead of their meshes.
	 *  Defaults to UPCGExDefaultActorSplineClassificator, which accepts none. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExActorSplineClassificator> SplineClassificator;

	/** Bounds evaluator. Defaults to UPCGExDefaultBoundsEvaluator. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;

	/** When true, the exporter builds embedded mesh/actor collections
	 *  and writes collection entry hashes instead of raw metadata. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bGenerateCollections = true;

	/** When true, material overrides from source components will be captured
	 *  and stored as material variants on the mesh collection entries. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bGenerateCollections"))
	bool bCaptureMaterialOverrides = true;

	/** When true and bGenerateCollections is enabled, capture per-instance property deltas
	 *  (CDO diff) on actor collection entries. Only applies to "Actor"-classified actors. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bGenerateCollections"))
	bool bCapturePropertyDeltas = true;

	/** Policy used when merging actor-component schemas into the generated embedded actor
	 *  collection's CollectionProperties + per-entry PropertyOverrides. Forwarded to
	 *  UPCGExActorCollection::RebuildPropertiesFromActorComponents and also mirrored onto the
	 *  embedded collection's own SchemaMergePolicy field so manual rebuilds stay consistent. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bGenerateCollections"))
	EPCGExSchemaMergePolicy SchemaMergePolicy = EPCGExSchemaMergePolicy::StrictTypeMatch;

	/** Run every export handler registered by other modules (e.g. cluster sketches). Off keeps only the
	 *  built-in Mesh / Actor / Level handlers. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bUseRegisteredHandlers = true;

	/** Controls how Name:Value tags are handled -- on actors, and on each mesh component of a mesh actor. */
	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExValueTagMode ValueTagMode = EPCGExValueTagMode::Parse;

	/** When true, write each actor's tag names as a joined string attribute on its actor point.
	 *  Only meaningful when ValueTagMode is NoParsing or ParseAndKeep. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bWriteInstanceTags = true;

	/** Output attribute name for the joined per-actor tag string. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bWriteInstanceTags && ValueTagMode != EPCGExValueTagMode::Parse", EditConditionHides))
	FName InstanceTagsAttributeName = FName("InstanceTags");

	virtual bool ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset) override;
	virtual bool ExportLevelData(const FPCGExLevelExportSource& Source, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext) override;

	/** Classify an actor into the slots it is harvested for. Override for custom logic; leaving OutSlots empty skips it.
	 *  Default behaviour:
	 *   - ALevelInstance with a non-null world asset → Levels.
	 *   - Otherwise, Meshes if MeshClassificator approves AND the actor owns at least one
	 *     UStaticMeshComponent (or subclass, e.g. ISMC) carrying a valid UStaticMesh and
	 *     contributing geometry; and Splines if SplineClassificator approves AND the actor
	 *     yields at least one spline (PCGExLevelExport::HasSplineContent). Both may apply.
	 *   - Neither → Actors.
	 *  An actor's classification tags and registered handlers' claims are decided BEFORE this runs. */
	virtual void ClassifyActor(AActor* Actor, PCGExLevelExport::FSlotSet& OutSlots) const;

	/** Called after all points are created, before collection generation. */
	virtual void OnExportComplete(UPCGDataAsset* OutAsset);

	/**
	 * Swaps ContentFilter, MeshClassificator, SplineClassificator and BoundsEvaluator for the project defaults
	 * (PCGEx | Collections settings) where they differ. A native instance resolves them in its constructor already; this
	 * covers an exporter whose subobjects were instanced from a class default built before the cache was filled. Never
	 * call it on an authored one.
	 */
	void ApplyProjectDefaults();

protected:
	/**
	 * Runs once per point data holding static mesh points (the Meshes pin), after the points exist and before value
	 * tags and OnMeshPointExported. Create the attributes OnMeshPointExported writes here, with their defaults: a value
	 * tag of the same name is then skipped with a warning instead of taking the name.
	 */
	virtual void DeclareMeshPointAttributes(UPCGMetadata* Metadata) const
	{
	}

	/**
	 * Runs once per exported static mesh point, after every built-in attribute is written. MetadataEntry is the point's
	 * entry key in Metadata. InstanceIndex is the instance of an instanced Component, INDEX_NONE for a plain one.
	 */
	virtual void OnMeshPointExported(const AActor* SourceActor, const UStaticMeshComponent* Component, int32 InstanceIndex, UPCGMetadata* Metadata, int64 MetadataEntry) const
	{
	}

#if WITH_EDITOR

private:
	/** Point data for one slot's items on its pin: transform, bounds, ActorName, value tags, handler attributes. */
	UPCGBasePointData* EmitSlotPoints(const FPCGExExportSlotWriter& Writer, const UPCGExLevelExportHandler* Handler, UPCGDataAsset* OutAsset) const;

	/** A slot without a collection: each item's data, copied into the asset with ActorName and value tags on @Data and
	 *  the authored tags as data tags, on the slot's pin. */
	void EmitRawSlotData(const FPCGExExportSlotWriter& Writer, const UPCGExLevelExportHandler* Handler, UPCGDataAsset* OutAsset) const;

	/** PerEntry slot: (re)build the embedded collection from the writer and write its picks on the pin. */
	void BuildEmbeddedSlot(const PCGExLevelExport::FHandlerRegistration& Registration, FPCGExExportSlotWriter& Writer, UPCGDataAsset* OutAsset, UPCGBasePointData* PointData, FPCGExLevelExportContext& OutContext) const;

#endif
};
