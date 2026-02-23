// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExLevelDataExporter.generated.h"

class UPCGDataAsset;

/**
 * Abstract base class for level → PCG data asset conversion.
 * Subclass in C++ or Blueprint to customize how a level's actors are
 * exported into a UPCGDataAsset during collection staging.
 *
 * Instanced on the collection via Instanced/EditInlineNew so that
 * derived classes can expose custom UPROPERTYs (filtering, transform
 * adjustments, etc.) directly in the collection's details panel.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class PCGEXCOLLECTIONS_API UPCGExLevelDataExporter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Export level data from the given world into the target data asset.
	 * The asset's TaggedData is already cleared before this is called.
	 *
	 * @param World      The loaded world to extract data from.
	 * @param OutAsset   The target data asset to populate. Outered to the owning collection.
	 * @return true if export succeeded and the asset contains valid data.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|LevelExport")
	bool ExportLevelData(UWorld* World, UPCGDataAsset* OutAsset);

	virtual bool ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset) { return false; }
};
