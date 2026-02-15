// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "EditorMode/PCGExValencyAssetTracker.h"
#include "EditorMode/PCGExValencyDirtyState.h"
#include "EditorMode/PCGExValencyReferenceTracker.h"

#include "PCGExValencyCageEditorMode.generated.h"

class APCGExValencyCageBase;
class APCGExValencyCage;
class APCGExValencyAssetPalette;
class AValencyContextVolume;
class FPCGExValencyEditorModeToolkit;
class IToolsContextRenderAPI;
class UPCGExValencyCageConnectorComponent;

/** Delegate fired when the scene cache (cages/volumes/palettes) changes */
DECLARE_MULTICAST_DELEGATE(FOnValencySceneChanged);

/**
 * Visibility flags for controlling which visualization layers are rendered.
 * Persists for the duration of the editor mode session.
 */
struct FValencyVisibilityFlags
{
	bool bShowConnections = true;
	bool bShowLabels = true;
	bool bShowConnectors = true;
	bool bShowVolumes = true;
	bool bShowGhostMeshes = true;
	bool bShowPatterns = true;
	bool bShowConstraints = true;
};

/**
 * Editor mode for Valency Cage authoring.
 * Provides viewport visualization of orbital connections, cage states, and placement tools.
 *
 * This class orchestrates:
 * - Cache management for cages and volumes
 * - Visualization via FPCGExValencyDrawHelper (ITF render delegates)
 * - Asset tracking via FPCGExValencyAssetTracker
 * - Input handling via toolkit command bindings
 *
 * Configuration is stored in UPCGExValencyEditorSettings (Project Settings > Plugins > PCGEx Valency Editor).
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExValencyCageEditorMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	/** Mode identifier */
	static const FEditorModeID ModeID;

	UPCGExValencyCageEditorMode();

	//~ Begin UEdMode Interface
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void ModeTick(float DeltaTime) override;
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	//~ End UEdMode Interface

	//~ Begin UBaseLegacyWidgetEdMode Widget Interface
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual bool ShouldDrawWidget() const override;
	//~ End UBaseLegacyWidgetEdMode Widget Interface

	// ========== Connector Management ==========

	/** Add a new connector to the given cage at its origin. Returns the new component. */
	UPCGExValencyCageConnectorComponent* AddConnectorToCage(APCGExValencyCageBase* Cage);

	/** Remove a connector component from its owning cage. */
	void RemoveConnector(UPCGExValencyCageConnectorComponent* Connector);

	/** Duplicate a connector component with a small spatial offset. Returns the new component. */
	UPCGExValencyCageConnectorComponent* DuplicateConnector(UPCGExValencyCageConnectorComponent* Connector);

	/** Get the currently selected connector component (from editor selection), or nullptr. */
	static UPCGExValencyCageConnectorComponent* GetSelectedConnector();

	/** Get the currently selected cage (from editor selection), or nullptr. */
	static APCGExValencyCageBase* GetSelectedCage();

protected:
	virtual void CreateToolkit() override;

	// ========== Connector Command Execute/CanExecute ==========

	void ExecuteAddConnector();
	bool CanExecuteAddConnector() const;
	void ExecuteRemoveConnector();
	bool CanExecuteRemoveConnector() const;
	void ExecuteDuplicateConnector();
	bool CanExecuteDuplicateConnector() const;
	void ExecuteCycleConnectorPolarity();
	bool CanExecuteCycleConnectorPolarity() const;

public:
	/** Get the cached cages array */
	const TArray<TWeakObjectPtr<APCGExValencyCageBase>>& GetCachedCages() const { return CachedCages; }

	/** Get the cached volumes array */
	const TArray<TWeakObjectPtr<AValencyContextVolume>>& GetCachedVolumes() const { return CachedVolumes; }

	/** Get the cached palettes array */
	const TArray<TWeakObjectPtr<APCGExValencyAssetPalette>>& GetCachedPalettes() const { return CachedPalettes; }

	/** Get the dirty state manager for marking actors dirty */
	FValencyDirtyStateManager& GetDirtyStateManager() { return DirtyStateManager; }

	/** Get the reference tracker for change propagation */
	FValencyReferenceTracker& GetReferenceTracker() { return ReferenceTracker; }

	/** Get the visualization visibility flags */
	const FValencyVisibilityFlags& GetVisibilityFlags() const { return VisibilityFlags; }

	/** Get mutable visibility flags (for toggle widgets) */
	FValencyVisibilityFlags& GetMutableVisibilityFlags() { return VisibilityFlags; }

	/** Delegate fired when the scene cache changes (cages/volumes/palettes added/removed) */
	FOnValencySceneChanged OnSceneChanged;

	/**
	 * Get the reference tracker from the active Valency editor mode.
	 * @return Pointer to tracker if mode is active, nullptr otherwise
	 */
	static FValencyReferenceTracker* GetActiveReferenceTracker();

protected:
	// ========== Rendering Callbacks (ITF delegates) ==========

	/** 3D viewport rendering via Interactive Tools Framework */
	void OnRenderCallback(IToolsContextRenderAPI* RenderAPI);

	/** 2D HUD rendering via Interactive Tools Framework */
	void OnDrawHUDCallback(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

	// ========== Cache Management ==========

	/** Collect all cages in the current level */
	void CollectCagesFromLevel();

	/** Collect all volumes in the current level */
	void CollectVolumesFromLevel();

	/** Collect all asset palettes in the current level */
	void CollectPalettesFromLevel();

	/** Master refresh - ensures all cages are properly initialized and connected */
	void RefreshAllCages();

	/** Initialize a single cage's orbitals and detect its connections */
	void InitializeCage(APCGExValencyCageBase* Cage);

	// ========== Actor Lifecycle ==========

	/** Callback when an actor is added to the level */
	void OnLevelActorAdded(AActor* Actor);

	/** Callback when an actor is deleted from the level */
	void OnLevelActorDeleted(AActor* Actor);

	/** Callback when selection changes */
	void OnSelectionChanged();

	/** Callback after Undo/Redo operation completes */
	void OnPostUndoRedo();

	// ========== Utilities ==========

	/** Set visibility of all cage debug components */
	void SetAllCageDebugComponentsVisible(bool bVisible);

	/** Cleanup stale manual connections from all cages */
	int32 CleanupAllManualConnections();

	/** Execute cleanup command (bound to toolkit command list) */
	void ExecuteCleanupCommand();

public:
	/** Redraw all viewports and invalidate viewport clients */
	void RedrawViewports();

	// ========== Mirror Ghost Preview ==========

	/** Set mirror ghost preview state (called by inspector widget on hover). AxisMask: bit 0=X, 1=Y, 2=Z */
	void SetMirrorGhostPreview(UPCGExValencyCageConnectorComponent* Connector, int32 AxisMask);

	/** Clear mirror ghost preview (called on unhover) */
	void ClearMirrorGhostPreview();

private:
	// ========== Cache State ==========

	/** Cached cages in level */
	TArray<TWeakObjectPtr<APCGExValencyCageBase>> CachedCages;

	/** Cached volumes in level */
	TArray<TWeakObjectPtr<AValencyContextVolume>> CachedVolumes;

	/** Cached asset palettes in level */
	TArray<TWeakObjectPtr<APCGExValencyAssetPalette>> CachedPalettes;

	/** Whether cache needs refresh */
	bool bCacheDirty = true;

	// ========== Visualization ==========

	/** Visibility toggle state for visualization layers */
	FValencyVisibilityFlags VisibilityFlags;

	// ========== Delegate Handles ==========

	FDelegateHandle OnActorAddedHandle;
	FDelegateHandle OnActorDeletedHandle;
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnPostUndoRedoHandle;
	FDelegateHandle OnRenderHandle;
	FDelegateHandle OnDrawHUDHandle;

	// ========== Asset Tracking ==========

	/** Asset tracker for monitoring selected actors */
	FPCGExValencyAssetTracker AssetTracker;

	// ========== Dirty State Management ==========

	/** Central dirty state manager for coalesced rebuilds */
	FValencyDirtyStateManager DirtyStateManager;

	/** Skip dirty processing for one frame after mode entry (allows system to stabilize) */
	bool bSkipNextDirtyProcess = false;

	// ========== Reference Tracking ==========

	/** Central reference tracker for change propagation */
	FValencyReferenceTracker ReferenceTracker;

	// ========== Mirror Ghost Preview ==========

	/** Axis mask being previewed (0 = none, bit 0=X, 1=Y, 2=Z) */
	int32 GhostMirrorAxisMask = 0;

	/** Connector being previewed */
	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> GhostMirrorConnector;

	/** Last Ctrl state during ghost preview (for redraw on modifier change) */
	bool bGhostLastCtrlState = false;
};
