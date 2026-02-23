// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "PCGExMeshSelectorStaged.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), DisplayName="[PCGEx] Staging Data")
class UPCGExMeshSelectorStaged : public UPCGMeshSelectorBase
{
	GENERATED_BODY()

public:
	virtual bool SelectMeshInstances(FPCGStaticMeshSpawnerContext& Context, const UPCGStaticMeshSpawnerSettings* Settings, const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& OutMeshInstances, UPCGBasePointData* OutPointData) const override;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bApplyMaterialOverrides = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForceDisableCollisions = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(InlineEditConditionToggle))
	bool bUseTemplateDescriptor = true;

	/** If enabled, will ignore the collection descriptor details and only push mesh, materials & tags from the collection. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(EditCondition="bUseTemplateDescriptor"))
	FPCGSoftISMComponentDescriptor TemplateDescriptor;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bUseTimeSlicing = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bOutputPoints = true;

	/** When enabled, silently skips input data that is missing the staging hash attribute instead of logging an error. */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bQuietMissingStagingDataWarning = false;
};
