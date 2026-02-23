// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Helpers/PCGExLevelDataExporter.h"

#include "PCGExDefaultLevelDataExporter.generated.h"

class AActor;
class UStaticMeshComponent;

UENUM()
enum class EPCGExActorExportType : uint8
{
	Mesh  = 0, // Has UStaticMeshComponent with valid mesh
	Actor = 1, // No static mesh → export as actor class reference
	Skip  = 2, // Exclude entirely
};

/**
 * Default level data exporter that replicates the engine's UPCGLevelToAsset behavior.
 *
 * For each qualifying actor in the level:
 * - Classifies actors as Mesh or Actor (or Skip)
 * - Creates a point at the actor's transform
 * - Stores mesh/actor references, materials, and bounds as metadata attributes
 * - Organizes output as typed tagged data entries ("Meshes", "Actors", "Instances")
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
	/** If non-empty, only actors with at least one of these tags are exported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FName> IncludeTags;

	/** Actors with any of these tags are excluded from export. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FName> ExcludeTags;

	/** If non-empty, only actors of these classes (or subclasses) are exported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<TSoftClassPtr<AActor>> IncludeClasses;

	/** Actors of these classes (or subclasses) are excluded from export. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<TSoftClassPtr<AActor>> ExcludeClasses;

	/** When true, the exporter builds embedded mesh/actor collections
	 *  and writes collection entry hashes instead of raw metadata. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bGenerateCollections = false;

	virtual bool ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset) override;

	/** Classify an actor. Override for custom logic.
	 *  Default: Mesh if has UStaticMeshComponent with valid mesh, Actor otherwise. */
	virtual EPCGExActorExportType ClassifyActor(AActor* Actor, UStaticMeshComponent*& OutMeshComponent) const;

	/** Called after all points are created, before collection generation. */
	virtual void OnExportComplete(UPCGDataAsset* OutAsset);
};
