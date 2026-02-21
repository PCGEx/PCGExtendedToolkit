// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Helpers/PCGExLevelDataExporter.h"

#include "PCGExDefaultLevelDataExporter.generated.h"

class AActor;

/**
 * Default level data exporter that replicates the engine's UPCGLevelToAsset behavior.
 *
 * For each qualifying actor in the level:
 * - Creates a point at the actor's transform
 * - Stores mesh references, materials, and bounds as metadata attributes
 * - Organizes output as tagged data entries in the target data asset
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

	virtual bool ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset) override;
};
