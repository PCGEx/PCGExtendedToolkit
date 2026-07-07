// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExSchemaMerging.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExActorMeshClassificator.h"
#include "Helpers/PCGExBoundsEvaluator.h"
#include "Helpers/PCGExLevelDataExporter.h"

#include "PCGExDefaultLevelDataExporter.generated.h"

class AActor;
class UStaticMeshComponent;

UENUM()
enum class EPCGExValueTagMode : uint8
{
	NoParsing UMETA(DisplayName = "No Parsing"),
	// Tags are written as-is to the instance tag string; no attribute parsing.
	Parse UMETA(DisplayName = "Parse"),
	// Plain tags become bool=true attributes; Name:Value tags become typed attributes.
	// The instance tag string is never written in this mode.
	ParseAndKeep UMETA(DisplayName = "Parse and Keep"),
	// Same as Parse, but the name-part of each value tag is also written to the instance tag string.
};

UENUM()
enum class EPCGExActorExportType : uint8
{
	Mesh = 0,
	// Has UStaticMeshComponent with valid mesh
	Actor = 1,
	// No static mesh → export as actor class reference
	Level = 2,
	// ALevelInstance → export the referenced UWorld asset to an embedded level collection
	Skip = 3,
	// Exclude entirely
};

/**
 * Default level data exporter that replicates the engine's UPCGLevelToAsset behavior.
 *
 * For each qualifying actor in the level:
 * - Classifies actors as Mesh or Actor (or Skip)
 * - Creates a point at the actor's transform
 * - Stores mesh/actor references, materials, and bounds as metadata attributes
 * - Organizes output as typed tagged data entries ("Meshes", "Actors")
 *
 * When bGenerateCollections is enabled, raw metadata is replaced with collection entry
 * hashes (int64 PCGEx/CollectionEntry), and embedded mesh/actor collections are built
 * for downstream consumption via Collection Map.
 *
 * Skips: hidden actors, editor-only actors, level script actors, info actors, brushes.
 * Supports tag/class include/exclude filtering (same pattern as level collection bounds).
 */
UCLASS(DisplayName = "Default Level Data Exporter")
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

	/** Controls how actor tags in the form Name:Value are handled.
	 *  Parse: plain tags → bool=true attributes; Name:Value tags → typed attributes; tag string not written.
	 *  ParseAndKeep: same as Parse, but the name-part of each value tag is also included in the tag string.
	 *  NoParsing: all tags are treated as plain strings (original behavior). */
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
	virtual bool ExportLevelData(UWorld* World, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext) override;

	/** Classify an actor. Override for custom logic.
	 *  Default behaviour:
	 *   - ALevelInstance with a non-null world asset → Level.
	 *   - Otherwise, if MeshClassificator approves AND the actor owns at least one
	 *     UStaticMeshComponent (or subclass, e.g. ISMC) carrying a valid UStaticMesh
	 *     and contributing geometry → Mesh.
	 *   - Otherwise → Actor.
	 *  An actor is either an Actor (full identity, property delta, etc.) or a Mesh
	 *  container (every SMC-derived component is harvested). The two are mutually
	 *  exclusive -- Actor-classified actors do NOT contribute mesh points, even when
	 *  they own ISMCs. */
	virtual EPCGExActorExportType ClassifyActor(AActor* Actor) const;

	/** Called after all points are created, before collection generation. */
	virtual void OnExportComplete(UPCGDataAsset* OutAsset);
};
