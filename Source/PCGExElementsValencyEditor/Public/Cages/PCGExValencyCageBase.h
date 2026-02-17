// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyAssetContainerBase.h"
#include "PCGExValencyCageOrbital.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "Core/PCGExValencyBondingRules.h"

#include "PCGExValencyCageBase.generated.h"

class UPCGExValencyConnectorSet;

class AValencyContextVolume;

/**
 * Type identifier for cage subclasses.
 * Replaces virtual bool type checks with branchless inline comparisons.
 */
UENUM()
enum class EPCGExValencyCageType : uint8
{
	/** Standard cage with asset registration and containment */
	Regular,
	/** Placeholder cage (boundary/wildcard/any constraint) */
	Null,
	/** Pattern position cage (proxies regular cages) */
	Pattern
};

/**
 * Reason for requesting a rebuild - used for logging and debugging.
 */
UENUM()
enum class EValencyRebuildReason : uint8
{
	/** A property with PCGEX_ValencyRebuild metadata changed */
	PropertyChange,

	/** The cage was moved */
	Movement,

	/** Assets were added/removed/changed */
	AssetChange,

	/** Orbital connections changed */
	ConnectionChange,

	/** Cascaded from another actor's change */
	ExternalCascade
};

/**
 * Abstract base class for Valency cage actors.
 * Cages represent potential node positions in a Valency graph and define
 * orbital connections to neighboring cages.
 *
 * Cages inherit their BondingRules and OrbitalSet from containing volumes.
 */
UCLASS(Abstract, HideCategories = (Rendering, Replication, Collision, HLOD, Physics, Networking, Input, LOD, Cooking))
class PCGEXELEMENTSVALENCYEDITOR_API APCGExValencyCageBase : public APCGExValencyAssetContainerBase
{
	GENERATED_BODY()

public:
	APCGExValencyCageBase();

	//~ Begin AActor Interface
	virtual void PostActorCreated() override;
	virtual void PostInitializeComponents() override;
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void BeginDestroy() override;
	//~ End AActor Interface

	//~ Begin APCGExValencyEditorActorBase Interface
	virtual void OnGhostRefreshRequested() override;
	virtual void OnRebuildMetaTagTriggered() override;
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End APCGExValencyEditorActorBase Interface

	//~ Begin Cage Interface

	/** Get the display name for this cage (used in editor UI) */
	virtual FString GetCageDisplayName() const;

	/** Get the cage type enum */
	FORCEINLINE EPCGExValencyCageType GetCageType() const { return CageType; }

	/** Check against a specific cage type */
	FORCEINLINE bool IsCageType(EPCGExValencyCageType Type) const { return CageType == Type; }

	/** Whether this is a null cage (placeholder - boundary/wildcard/any based on mode) */
	FORCEINLINE bool IsNullCage() const { return CageType == EPCGExValencyCageType::Null; }

	/** Whether this is a pattern cage (for filtering connections) */
	FORCEINLINE bool IsPatternCage() const { return CageType == EPCGExValencyCageType::Pattern; }

	/** Get the effective orbital set (from volume or override) */
	UPCGExValencyOrbitalSet* GetEffectiveOrbitalSet() const;

	/** Get the effective bonding rules (from volume or override) */
	UPCGExValencyBondingRules* GetEffectiveBondingRules() const;

	/** Get the effective probe radius */
	float GetEffectiveProbeRadius() const;

	/**
	 * Get whether orbital directions should be transformed by this cage's rotation.
	 * Resolves TransformMode (Inherit uses OrbitalSet setting, Force overrides).
	 */
	bool ShouldTransformOrbitalDirections() const;

	/** Get the orbitals array */
	const TArray<FPCGExValencyCageOrbital>& GetOrbitals() const { return Orbitals; }

	/** Get mutable orbitals array */
	TArray<FPCGExValencyCageOrbital>& GetOrbitals() { return Orbitals; }

	/** Check if this cage has a connection to another cage */
	bool HasConnectionTo(const APCGExValencyCageBase* OtherCage) const;

	/** Get the orbital index of a connection to another cage (-1 if not connected) */
	int32 GetOrbitalIndexTo(const APCGExValencyCageBase* OtherCage) const;

	/** Called when a containing volume changes */
	virtual void OnContainingVolumeChanged(AValencyContextVolume* Volume);

	//~ End Cage Interface

	/** Recalculate which volumes contain this cage */
	void RefreshContainingVolumes();

	/** Get the list of volumes that contain this cage */
	const TArray<TWeakObjectPtr<AValencyContextVolume>>& GetContainingVolumes() const { return ContainingVolumes; }

	/** Check if an actor should be ignored based on containing volumes' ignore rules */
	bool ShouldIgnoreActor(const AActor* Actor) const;

	/** Initialize orbitals from the orbital set */
	void InitializeOrbitalsFromSet();

	/** Detect and connect to nearby cages using probe radius. Returns true if connections changed. */
	virtual bool DetectNearbyConnections();

protected:
	/**
	 * Filter to determine if a candidate cage should be considered for auto-connection.
	 * Override in subclasses to restrict connection targets (e.g., pattern cages only connect to pattern cages).
	 * @param CandidateCage The cage being considered for connection
	 * @return true if this cage should consider connecting to the candidate
	 */
	virtual bool ShouldConsiderCageForConnection(const APCGExValencyCageBase* CandidateCage) const { return true; }

public:

	/**
	 * Remove null/invalid entries from all orbital manual connection lists.
	 * @return Total number of stale entries removed
	 */
	int32 CleanupManualConnections();

	/**
	 * Notify this cage that a related cage has moved or changed.
	 * Triggers a refresh of connections if the moved cage affects us.
	 * @param MovedCage The cage that was moved/changed
	 */
	void OnRelatedCageMoved(APCGExValencyCageBase* MovedCage);

	/**
	 * Notify all cages in the world that this cage has moved.
	 * Called automatically from PostEditMove.
	 * @deprecated Use NotifyAffectedCagesOfMovement for better performance
	 */
	void NotifyAllCagesOfMovement();

	/**
	 * Notify only cages affected by this cage's movement using spatial registry.
	 * More efficient than NotifyAllCagesOfMovement for large scenes.
	 * @param OldPosition Position before the move
	 * @param NewPosition Position after the move
	 */
	void NotifyAffectedCagesOfMovement(const FVector& OldPosition, const FVector& NewPosition);

	/**
	 * Set visibility of internal debug components.
	 * Called by editor mode to hide built-in visuals when custom mode drawing is active.
	 * @param bVisible True to show components, false to hide
	 */
	virtual void SetDebugComponentsVisible(bool bVisible);

	//~ Begin Ghost Mesh Interface

	/**
	 * Rebuild ghost mesh components for this cage.
	 * Override in subclasses to create ghost previews (mirror ghosts, proxy ghosts, etc.).
	 * Base implementation does nothing.
	 */
	virtual void RefreshGhostMeshes();

	/**
	 * Clear all ghost mesh components (tag-based cleanup).
	 * Finds and destroys all components tagged with the ghost mesh tag,
	 * including orphaned components from actor duplication.
	 */
	void ClearGhostMeshes();

	//~ End Ghost Mesh Interface

public:
	/** If false, this cage is completely excluded from compilation (not inherited, mirrored, or built into bonding rules) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage", meta = (PCGEX_ValencyRebuild, ToolTip = "Include this cage in compilation"))
	bool bEnabledForCompilation = true;

	/** Optional display name for this cage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage")
	FString CageName;

	/**
	 * Probe radius for detecting nearby cages.
	 * -1 = use volume's default radius.
	 * 0 = receive-only (other cages can detect me, I don't detect them).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Detection", meta = (ClampMin = "-1.0"))
	float ProbeRadius = -1.0f;

	/**
	 * Whether to apply cage rotation to orbital directions.
	 * If true, orbital directions are transformed by this cage's rotation.
	 * If false, orbitals use world-space directions (useful for copy-paste patterns).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Transform")
	bool bTransformOrbitalDirections = true;

	/** Orbital connections to other cages */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Orbitals", meta = (TitleProperty = "OrbitalName"))
	TArray<FPCGExValencyCageOrbital> Orbitals;

	// ========== Connectors ==========

	/**
	 * If enabled, automatically extract connectors from the cage's effective assets
	 * (meshes from registered assets, palettes, and mirror sources) at compile time.
	 * Mesh sockets matching ConnectorSet definitions are converted to module connectors.
	 * Connector components can override auto-extracted connectors by name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Connectors")
	bool bReadConnectorsFromAssets = false;

	/** Get the effective connector set (from volume or bonding rules) */
	UPCGExValencyConnectorSet* GetEffectiveConnectorSet() const;

	/**
	 * Get all connector components attached to this cage.
	 * @param OutComponents Array to fill with connector components
	 */
	void GetConnectorComponents(TArray<class UPCGExValencyCageConnectorComponent*>& OutComponents) const;

	/** Check if this cage has any connector components */
	bool HasConnectors() const;

	/** Check if this cage has any plug connector components */
	bool HasPlugConnectors() const;

	/**
	 * Find a connector component by identifier.
	 * @param Identifier The connector identifier to search for
	 * @return Pointer to the component, or nullptr if not found
	 */
	class UPCGExValencyCageConnectorComponent* FindConnectorByIdentifier(const FName& Identifier) const;

	/**
	 * Find a connector component by type (returns first match).
	 * @param ConnectorType The connector type to search for
	 * @return Pointer to the component, or nullptr if not found
	 */
	class UPCGExValencyCageConnectorComponent* FindConnectorByType(const FName& ConnectorType) const;

	/**
	 * Create connector components from a static mesh asset.
	 * Extracts UStaticMeshSocket data and creates attached connector components.
	 * @param Mesh The static mesh to extract sockets from
	 * @param DefaultConnectorType The connector type to assign to created components
	 * @param DefaultPolarity The polarity to assign to created components
	 * @return Number of connector components created
	 */
	int32 CreateConnectorComponentsFromMesh(UStaticMesh* Mesh, const FName& DefaultConnectorType, EPCGExConnectorPolarity DefaultPolarity = EPCGExConnectorPolarity::Universal);

	/**
	 * Request a rebuild through the unified dirty state system.
	 * This is the preferred method for triggering rebuilds - it goes through
	 * the dirty state manager for proper coalescing and dependency cascade.
	 * @param Reason The reason for the rebuild request (for logging)
	 */
	void RequestRebuild(EValencyRebuildReason Reason);

	/**
	 * Trigger auto-rebuild for containing volumes if conditions are met.
	 * Consolidates the common rebuild triggering logic.
	 * Checks: Valency mode active, bAutoRebuildOnChange enabled.
	 * @return True if a rebuild was triggered
	 * @deprecated Use RequestRebuild() instead for proper coalescing
	 */
	bool TriggerAutoRebuildIfNeeded();

	/**
	 * Trigger auto-rebuild for specific volumes if conditions are met.
	 * @param Volumes Volumes to check for auto-rebuild
	 * @return True if a rebuild was triggered
	 */
	static bool TriggerAutoRebuildForVolumes(const TArray<AValencyContextVolume*>& Volumes);

protected:
	/** Cage type identifier, set in subclass constructors */
	EPCGExValencyCageType CageType = EPCGExValencyCageType::Regular;

	/** Volumes that contain this cage (transient, not saved) */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Cage|Debug")
	TArray<TWeakObjectPtr<AValencyContextVolume>> ContainingVolumes;

	/** Cached orbital set (resolved from volumes or override) */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPCGExValencyOrbitalSet> CachedOrbitalSet;

	/** Whether orbital initialization is needed */
	bool bNeedsOrbitalInit = true;

	/** Flag set when cage is newly created (not loaded from disk) */
	bool bIsNewlyCreated = false;

	/** Last position used for live drag updates (throttling) */
	FVector LastDragUpdatePosition = FVector::ZeroVector;

	/** Minimum distance to trigger a live drag update */
	static constexpr float DragUpdateThreshold = 10.0f;

	/** Whether we're currently being dragged */
	bool bIsDragging = false;

	/** Position when drag started (for computing affected cages) */
	FVector DragStartPosition = FVector::ZeroVector;

	/** Volumes containing this cage before drag started (for membership change detection) */
	TArray<TWeakObjectPtr<AValencyContextVolume>> VolumesBeforeDrag;

	/** Auto-connections before drag started (for connection change detection) */
	TArray<TWeakObjectPtr<APCGExValencyCageBase>> ConnectionsBeforeDrag;

	/** Capture current auto-connections state (for later comparison) */
	void CaptureConnectionState(TArray<TWeakObjectPtr<APCGExValencyCageBase>>& OutConnections) const;

	/** Check if current connections differ from captured state */
	bool HaveConnectionsChanged(const TArray<TWeakObjectPtr<APCGExValencyCageBase>>& OldConnections) const;

	/** Update connections during drag using spatial registry */
	void UpdateConnectionsDuringDrag();

	/**
	 * Check for volume membership changes and trigger auto-rebuild if needed.
	 * Called after RefreshContainingVolumes() when drag finishes.
	 * @param OldVolumes Volumes that contained this cage before the move
	 */
	void HandleVolumeMembershipChange(const TArray<TWeakObjectPtr<AValencyContextVolume>>& OldVolumes);
};
