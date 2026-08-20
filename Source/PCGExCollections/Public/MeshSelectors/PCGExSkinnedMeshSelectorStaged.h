// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "MeshSelectors/PCGSkinnedMeshSelector.h"
#include "PCGExSkinnedMeshSelectorStaged.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), DisplayName="[PCGEx] Staging Data (Skinned)", meta=(PCGExNodeLibraryDoc="staging/utilities/staging-data-skinned-mesh-selector"))
class UPCGExSkinnedMeshSelectorStaged : public UPCGSkinnedMeshSelector
{
	GENERATED_BODY()

public:
	virtual bool SelectInstances(FPCGSkinnedMeshSpawnerContext& Context, const UPCGSkinnedMeshSpawnerSettings* Settings, const UPCGPointData* InPointData, TArray<FPCGSkinnedMeshInstanceList>& OutMeshInstances, UPCGPointData* OutPointData) const override;

	/** Staging layer this selector reads staged picks from (and strips from output points). None = default layer (PCGEx/CollectionEntry); otherwise the layer name is appended (PCGEx/CollectionEntry/<layer>). Other layers' attributes pass through untouched. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, AdvancedDisplay)
	FName StagingLayer = NAME_None;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bApplyMaterialOverrides = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForceDisableCollisions = false;

	// If enabled, will ignore the collection descriptor details and only push asset, materials & tags from the collection. Uses the inherited TemplateDescriptor from UPCGSkinnedMeshSelector.
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bUseTemplateDescriptor = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bUseTimeSlicing = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bOutputPoints = true;

	// When enabled, silently skips input data that is missing the staging hash attribute instead of logging an error.
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bQuietMissingStagingDataWarning = false;
};
