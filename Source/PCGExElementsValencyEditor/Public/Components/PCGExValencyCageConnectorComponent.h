// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/PCGExValencyConnectorSet.h"

#include "PCGExValencyCageConnectorComponent.generated.h"

class UPCGExValencyConnectorSet;
class UStaticMesh;

/**
 * A connector component attached to a Valency cage.
 * Connectors are non-directional connection points that map to orbitals during compilation.
 * Unlike orbitals (which are direction-based), connectors have explicit transforms and type identity.
 */
UCLASS(BlueprintType, ClassGroup = PCGEx, meta = (BlueprintSpawnableComponent), HideCategories = (Mobility, LOD, Collision, Physics, Rendering, Navigation, Cooking))
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExValencyCageConnectorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPCGExValencyCageConnectorComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	//~ End UActorComponent Interface

	// ========== Connector Properties ==========

	/** Connector identifier (unique per cage, used for socket matching and pipeline output). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector")
	FName Identifier;

	/** Connector type (references ConnectorSet.ConnectorTypes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector")
	FName ConnectorType;

	/** Connector polarity - determines connection compatibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector")
	EPCGExConnectorPolarity Polarity = EPCGExConnectorPolarity::Universal;

	/** Whether this connector is enabled (disabled connectors are ignored during compilation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector")
	bool bEnabled = true;

	/** Whether this connector is inherited by cages that mirror this cage's connectors */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector")
	bool bInheritable = true;

	// ========== Constraints ==========

	/** Per-instance constraint overrides */
	UPROPERTY(EditAnywhere, Category = "Connector|Constraints", meta=(BaseStruct="/Script/PCGExElementsValency.PCGExConnectorConstraint", ExcludeBaseStruct))
	TArray<FInstancedStruct> ConstraintOverrides;

	/** How instance overrides interact with type-level default constraints */
	UPROPERTY(EditAnywhere, Category = "Connector|Constraints", meta=(DisplayAfter="ConstraintOverrides"))
	EPCGExConstraintOverrideMode OverrideMode = EPCGExConstraintOverrideMode::Append;

	// ========== Orbital Override ==========

	/** Override automatic orbital index assignment for this connector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector", AdvancedDisplay)
	bool bManualOrbitalOverride = false;

	/** Manual orbital index (0-63). Only used when bManualOrbitalOverride is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector", AdvancedDisplay,
		meta = (EditCondition = "bManualOrbitalOverride", ClampMin = "0", ClampMax = "63"))
	int32 ManualOrbitalIndex = 0;

	// ========== Mesh Integration ==========

	/** Optional reference to a mesh socket name to inherit transform from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector|Mesh Integration", AdvancedDisplay)
	FName MeshSocketName;

	/** If enabled, automatically match and inherit transform from a mesh socket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector|Mesh Integration", AdvancedDisplay)
	bool bMatchMeshSocketTransform = false;

	/** If enabled, this connector component overrides any auto-extracted connector with the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector|Mesh Integration", AdvancedDisplay)
	bool bOverrideAutoExtracted = true;

	// ========== Visualization ==========

	/** Debug visualization color override. If (0,0,0,0), uses the color from ConnectorSet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connector|Visualization", AdvancedDisplay)
	FLinearColor DebugColorOverride = FLinearColor(0, 0, 0, 0);

	// ========== Methods ==========

	FLinearColor GetEffectiveDebugColor(const UPCGExValencyConnectorSet* ConnectorSet) const;
	FTransform GetConnectorWorldTransform() const { return GetComponentTransform(); }
	FTransform GetConnectorLocalTransform() const { return GetRelativeTransform(); }
	bool SyncTransformFromMeshSocket(UStaticMesh* Mesh);

protected:
	void GenerateDefaultIdentifier();
	void RequestCageRebuild();
};
