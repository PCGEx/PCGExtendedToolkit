// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "PCGExPropertyFloatPacker.h"
#include "PCGExMeshSelectorStaged.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), DisplayName="[PCGEx] Staging Data")
class UPCGExMeshSelectorStaged : public UPCGMeshSelectorBase
{
	GENERATED_BODY()

public:
	virtual bool SelectMeshInstances(FPCGStaticMeshSpawnerContext& Context, const UPCGStaticMeshSpawnerSettings* Settings, const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& OutMeshInstances, UPCGBasePointData* OutPointData) const override;

	/** Staging layer this selector reads staged picks from (and strips from output points). None = default layer (PCGEx/CollectionEntry); otherwise the layer name is appended (PCGEx/CollectionEntry/<layer>). Other layers' attributes pass through untouched. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, AdvancedDisplay)
	FName StagingLayer = NAME_None;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bApplyMaterialOverrides = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForceDisableCollisions = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(InlineEditConditionToggle))
	bool bUseTemplateDescriptor = true;

	/** Ignores per-entry collection descriptors, pushing only mesh, materials & tags. On by default for legacy reasons -- turn it off if your collections set per-entry details. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(DisplayName="Descriptor Override", EditCondition="bUseTemplateDescriptor"))
	FPCGSoftISMComponentDescriptor TemplateDescriptor;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bUseTimeSlicing = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bOutputPoints = true;

	/** When enabled, silently skips input data that is missing the staging hash attribute instead of logging an error. */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bQuietMissingStagingDataWarning = false;

	/** Per-entry collection properties packed into the spawned component's Custom Primitive Data. */
	// No ShowOnlyInnerProperties: it re-categorizes promoted members under THEIR own Category,
	// scattering them into "Settings" instead of showing them here.
	UPROPERTY(EditAnywhere, Category = "MeshSelector|Custom Primitive Data", meta=(DisplayName="Layout"))
	FPCGExPackedFloatLayout CustomPrimitiveDataLayout;

#if WITH_EDITORONLY_DATA
	/** Logs the resolved layout and each entry's floats. PCGNoHash: hashing diagnostics would rebuild every ISM on toggle. */
	UPROPERTY(EditAnywhere, Category = "MeshSelector|Custom Primitive Data", meta=(DisplayName="Debug Layout", PCGNoHash))
	bool bDebugCustomPrimitiveData = false;
#endif

#if WITH_EDITOR
	/** Drives Layout > Populate From. A CallInEditor button can't: FObjectDetails renders those from
	 *  a class layout that only runs for a details view's root objects, never a nested sub-object. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
