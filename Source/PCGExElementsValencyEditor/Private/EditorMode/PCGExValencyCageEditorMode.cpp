// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExValencyCageEditorMode.h"

#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Selection.h"
#include "ScopedTransaction.h"
#include "LevelEditorViewport.h"
#include "UnrealEdGlobals.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "ToolContextInterfaces.h"

#include "Editor/UnrealEdEngine.h"
#include "EditorMode/PCGExConstraintVisualizer.h"
#include "EditorMode/PCGExValencyDrawHelper.h"
#include "EditorMode/PCGExValencyEditorModeToolkit.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Volumes/ValencyContextVolume.h"
#include "PCGExValencyEditorSettings.h"
#include "Framework/Application/SlateApplication.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExValencyCageEditorMode)

const FEditorModeID UPCGExValencyCageEditorMode::ModeID = TEXT("PCGExValencyCageEditorMode");

UPCGExValencyCageEditorMode::UPCGExValencyCageEditorMode()
{
	Info = FEditorModeInfo(
		ModeID,
		NSLOCTEXT("PCGExValency", "ValencyCageModeName", "PCGEx | Valency"),
		FSlateIcon(),
		true,
		MAX_int32);
}

FValencyReferenceTracker* UPCGExValencyCageEditorMode::GetActiveReferenceTracker()
{
	if (GLevelEditorModeTools().IsModeActive(ModeID))
	{
		if (UPCGExValencyCageEditorMode* Mode = Cast<UPCGExValencyCageEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode(ModeID)))
		{
			return &Mode->ReferenceTracker;
		}
	}
	return nullptr;
}

void UPCGExValencyCageEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FPCGExValencyEditorModeToolkit());
}

void UPCGExValencyCageEditorMode::Enter()
{
	Super::Enter();

	// Bind to ITF rendering delegates (available after UEdMode::Enter creates the ToolsContext)
	if (UEditorInteractiveToolsContext* ToolsContext = GetInteractiveToolsContext())
	{
		OnRenderHandle = ToolsContext->OnRender.AddUObject(this, &UPCGExValencyCageEditorMode::OnRenderCallback);
		OnDrawHUDHandle = ToolsContext->OnDrawHUD.AddUObject(this, &UPCGExValencyCageEditorMode::OnDrawHUDCallback);
	}

	// Register keyboard command bindings on the toolkit
	if (Toolkit.IsValid())
	{
		TSharedPtr<FUICommandList> CommandList = Toolkit->GetToolkitCommands();
		if (CommandList.IsValid())
		{
			CommandList->MapAction(
				FValencyEditorCommands::Get().CleanupConnections,
				FExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::ExecuteCleanupCommand));

			CommandList->MapAction(
				FValencyEditorCommands::Get().AddConnector,
				FExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::ExecuteAddConnector),
				FCanExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::CanExecuteAddConnector));

			CommandList->MapAction(
				FValencyEditorCommands::Get().RemoveConnector,
				FExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::ExecuteRemoveConnector),
				FCanExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::CanExecuteRemoveConnector));

			CommandList->MapAction(
				FValencyEditorCommands::Get().DuplicateConnector,
				FExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::ExecuteDuplicateConnector),
				FCanExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::CanExecuteDuplicateConnector));

			CommandList->MapAction(
				FValencyEditorCommands::Get().CycleConnectorPolarity,
				FExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::ExecuteCycleConnectorPolarity),
				FCanExecuteAction::CreateUObject(this, &UPCGExValencyCageEditorMode::CanExecuteCycleConnectorPolarity));
		}
	}

	// Bind to actor add/delete events to keep cache up to date
	if (GEditor)
	{
		OnActorAddedHandle = GEditor->OnLevelActorAdded().AddUObject(this, &UPCGExValencyCageEditorMode::OnLevelActorAdded);
		OnActorDeletedHandle = GEditor->OnLevelActorDeleted().AddUObject(this, &UPCGExValencyCageEditorMode::OnLevelActorDeleted);

		// Bind to selection changes for asset tracking
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddLambda([this](UObject*)
		{
			OnSelectionChanged();
		});

		// Bind to Undo/Redo for handling state restoration
		OnPostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddUObject(this, &UPCGExValencyCageEditorMode::OnPostUndoRedo);
	}

	// Collect and fully initialize all cages
	CollectCagesFromLevel();
	CollectVolumesFromLevel();
	CollectPalettesFromLevel();
	RefreshAllCages();

	// Initialize asset tracker with our cache references
	AssetTracker.Initialize(CachedCages, CachedVolumes, CachedPalettes);

	// Initialize reference tracker FIRST (builds dependency graph for change propagation)
	ReferenceTracker.Initialize(&CachedCages, &CachedVolumes, &CachedPalettes);

	// Initialize dirty state manager with all cached actors and reference tracker
	DirtyStateManager.Initialize(CachedCages, CachedVolumes, CachedPalettes, &ReferenceTracker);

	// Capture current selection state - handles case where actors are already selected
	// when entering Valency mode (OnSelectionChanged won't fire for existing selection)
	OnSelectionChanged();

	bCacheDirty = false;

	// Skip first dirty process to allow system to stabilize after mode entry
	bSkipNextDirtyProcess = true;

	// Notify panel/widgets that caches are now populated (toolkit was created
	// in Super::Enter() before caches were collected, so initial content is stale)
	OnSceneChanged.Broadcast();
}

void UPCGExValencyCageEditorMode::Exit()
{
	// Unbind ITF rendering delegates
	if (UEditorInteractiveToolsContext* ToolsContext = GetInteractiveToolsContext())
	{
		ToolsContext->OnRender.Remove(OnRenderHandle);
		ToolsContext->OnDrawHUD.Remove(OnDrawHUDHandle);
	}
	OnRenderHandle.Reset();
	OnDrawHUDHandle.Reset();

	// Unbind actor add/delete events
	if (GEditor)
	{
		GEditor->OnLevelActorAdded().Remove(OnActorAddedHandle);
		GEditor->OnLevelActorDeleted().Remove(OnActorDeletedHandle);
		GEditor->GetSelectedActors()->SelectionChangedEvent.Remove(OnSelectionChangedHandle);
	}
	FEditorDelegates::PostUndoRedo.Remove(OnPostUndoRedoHandle);

	OnActorAddedHandle.Reset();
	OnActorDeletedHandle.Reset();
	OnSelectionChangedHandle.Reset();
	OnPostUndoRedoHandle.Reset();

	// Clear tracking state
	AssetTracker.Reset();
	DirtyStateManager.Reset();
	ReferenceTracker.Reset();

	CachedCages.Empty();
	CachedVolumes.Empty();
	CachedPalettes.Empty();

	Super::Exit();
}

void UPCGExValencyCageEditorMode::OnRenderCallback(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	if (!PDI)
	{
		return;
	}

	// Refresh cache if needed
	if (bCacheDirty)
	{
		CollectCagesFromLevel();
		CollectVolumesFromLevel();
		CollectPalettesFromLevel();
		RefreshAllCages();
		bCacheDirty = false;
	}

	// Draw volumes first (background)
	if (VisibilityFlags.bShowVolumes)
	{
		for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : CachedVolumes)
		{
			if (AValencyContextVolume* Volume = VolumePtr.Get())
			{
				FPCGExValencyDrawHelper::DrawVolume(PDI, Volume);
			}
		}
	}

	// Draw asset palettes
	if (VisibilityFlags.bShowVolumes) // Palettes are volume-like containers
	{
		for (const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr : CachedPalettes)
		{
			if (APCGExValencyAssetPalette* Palette = PalettePtr.Get())
			{
				FPCGExValencyDrawHelper::DrawPalette(PDI, Palette);
			}
		}
	}

	// Draw cages (connections and patterns)
	if (VisibilityFlags.bShowConnections)
	{
		for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
		{
			if (APCGExValencyCageBase* Cage = CagePtr.Get())
			{
				FPCGExValencyDrawHelper::DrawCage(PDI, Cage);
			}
		}
	}

	// Draw connectors for all cages
	if (VisibilityFlags.bShowConnectors)
	{
		for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
		{
			if (APCGExValencyCageBase* Cage = CagePtr.Get())
			{
				FPCGExValencyDrawHelper::DrawCageConnectors(PDI, Cage);
			}
		}

		// Draw ghost mirror preview
		if (GhostMirrorAxisMask != 0)
		{
			if (UPCGExValencyCageConnectorComponent* GhostConn = GhostMirrorConnector.Get())
			{
				if (GhostConn->bEnabled)
				{
					const UPCGExValencyEditorSettings* Settings = UPCGExValencyEditorSettings::Get();
					if (Settings)
					{
						const bool bCtrl = FSlateApplication::Get().GetModifierKeys().IsControlDown();
						const FTransform T = FPCGExValencyDrawHelper::ComputeMirroredTransform(
							GhostConn->GetRelativeTransform(), GhostMirrorAxisMask, bCtrl);

						// Convert to world space
						const AActor* GhostOwner = GhostConn->GetOwner();
						const FTransform GhostWorld = GhostOwner ? T * GhostOwner->GetActorTransform() : T;
						const FVector GhostLoc = GhostWorld.GetLocation();
						const FQuat GhostRot = GhostWorld.GetRotation();

						// Get connector color at reduced alpha
						const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(GhostOwner);
						const UPCGExValencyConnectorSet* ConnSet = OwnerCage ? OwnerCage->GetEffectiveConnectorSet() : nullptr;
						FLinearColor GhostColor = GhostConn->GetEffectiveDebugColor(ConnSet);
						GhostColor.A = 0.35f;

						FPCGExValencyDrawHelper::DrawConnectorShape(
							PDI, GhostLoc,
							GhostRot.GetForwardVector(), GhostRot.GetRightVector(), GhostRot.GetUpVector(),
							GhostConn->Polarity, Settings->ConnectorVisualizerSize, Settings->ConnectorArrowLength,
							GhostColor, false);

						// Draw ghost constraints at the preview transform
						if (VisibilityFlags.bShowConstraints)
						{
							FLinearColor GhostConstraintColor = Settings->ConstraintZoneColor;
							GhostConstraintColor.A *= 0.4f;
							FPCGExValencyDrawHelper::DrawConnectorConstraintsAt(
								PDI, GhostConn, GhostWorld,
								EPCGExConstraintDetailLevel::Zone, GhostConstraintColor);
						}
					}
				}
			}
		}
	}

	// Draw constraint visualization
	if (VisibilityFlags.bShowConstraints)
	{
		const UPCGExValencyCageConnectorComponent* SelectedConn = GetSelectedConnector();

		// Determine which cages are selected for detail level decisions
		TSet<const APCGExValencyCageBase*> SelectedCageSet;
		if (GEditor)
		{
			USelection* Selection = GEditor->GetSelectedActors();
			for (FSelectionIterator It(*Selection); It; ++It)
			{
				if (const APCGExValencyCageBase* SelectedCage = Cast<APCGExValencyCageBase>(*It))
				{
					SelectedCageSet.Add(SelectedCage);
				}
			}
		}

		for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
		{
			APCGExValencyCageBase* Cage = CagePtr.Get();
			if (!Cage) { continue; }

			const bool bCageSelected = SelectedCageSet.Contains(Cage);

			if (SelectedConn && SelectedConn->GetOwner() == Cage)
			{
				// This cage has the selected connector — Detail for selected, Zone for siblings
				FPCGExValencyDrawHelper::DrawCageConstraints(
					PDI, Cage, EPCGExConstraintDetailLevel::Zone, SelectedConn);
			}
			else if (bCageSelected)
			{
				// Cage selected but no connector — Zone level
				FPCGExValencyDrawHelper::DrawCageConstraints(
					PDI, Cage, EPCGExConstraintDetailLevel::Zone);
			}
			else
			{
				// Not selected — Indicator only
				FPCGExValencyDrawHelper::DrawCageConstraints(
					PDI, Cage, EPCGExConstraintDetailLevel::Indicator);
			}
		}
	}
}

void UPCGExValencyCageEditorMode::OnDrawHUDCallback(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	const FSceneView* View = RenderAPI->GetSceneView();
	if (!Canvas || !View)
	{
		return;
	}

	if (!VisibilityFlags.bShowLabels)
	{
		return;
	}

	// Determine which cages are selected for label color
	TSet<const APCGExValencyCageBase*> SelectedCages;
	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (const APCGExValencyCageBase* SelectedCage = Cast<APCGExValencyCageBase>(*It))
			{
				SelectedCages.Add(SelectedCage);
			}
		}
	}

	// Draw labels for all cages
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (const APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			const bool bIsSelected = SelectedCages.Contains(Cage);
			FPCGExValencyDrawHelper::DrawCageLabels(Canvas, View, Cage, bIsSelected);
		}
	}

	// Draw labels for asset palettes
	TSet<const APCGExValencyAssetPalette*> SelectedPalettes;
	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (const APCGExValencyAssetPalette* SelectedPalette = Cast<APCGExValencyAssetPalette>(*It))
			{
				SelectedPalettes.Add(SelectedPalette);
			}
		}
	}

	for (const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr : CachedPalettes)
	{
		if (const APCGExValencyAssetPalette* Palette = PalettePtr.Get())
		{
			const bool bIsSelected = SelectedPalettes.Contains(Palette);
			FPCGExValencyDrawHelper::DrawPaletteLabels(Canvas, View, Palette, bIsSelected);
		}
	}
}

bool UPCGExValencyCageEditorMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	// Allow selection of all actors
	return true;
}

bool UPCGExValencyCageEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	// Forward all clicks to the component visualizer manager.
	// For connector hit proxies: routes to VisProxyHandleClick (enables viewport diamond clicking).
	// For other proxies: calls ClearActiveComponentVis (clears stale gizmo/state).
	if (GUnrealEd)
	{
		if (GUnrealEd->ComponentVisManager.HandleClick(InViewportClient, HitProxy, Click))
		{
			return true;
		}
	}

	return Super::HandleClick(InViewportClient, HitProxy, Click);
}

void UPCGExValencyCageEditorMode::ExecuteCleanupCommand()
{
	CleanupAllManualConnections();
}

void UPCGExValencyCageEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);

	// Update asset tracking if enabled - this marks cages/palettes dirty
	if (AssetTracker.IsEnabled())
	{
		TSet<APCGExValencyCage*> AffectedCages;
		TSet<APCGExValencyAssetPalette*> AffectedPalettes;
		if (AssetTracker.Update(AffectedCages, AffectedPalettes))
		{
			// Mark affected cages dirty
			for (APCGExValencyCage* Cage : AffectedCages)
			{
				if (Cage)
				{
					DirtyStateManager.MarkCageDirty(Cage, EValencyDirtyFlags::Assets);
				}
			}
			// Mark affected palettes dirty (will cascade to mirroring cages)
			for (APCGExValencyAssetPalette* Palette : AffectedPalettes)
			{
				if (Palette)
				{
					DirtyStateManager.MarkPaletteDirty(Palette, EValencyDirtyFlags::Assets);
				}
			}
		}
	}

	// Process all dirty state once per frame (coalesced rebuilds)
	// Skip dirty processing on first frame after mode entry (system stabilization)
	// NOTE: We do NOT reset dirty state - changes are queued for the next frame
	if (bSkipNextDirtyProcess)
	{
		bSkipNextDirtyProcess = false;
		// Don't clear dirty state - let it accumulate for processing on next frame
	}
	else if (DirtyStateManager.ProcessDirty())
	{
		RedrawViewports();
	}

	// Execute deferred PCG regeneration for volumes that have been quiet for at least one frame.
	// During rapid interactive changes (slider drags), RegeneratePCGActors just resets the
	// pending frame counter each tick. The actual flush+generate only runs here once the
	// slider stops and a full frame passes without a new request.
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : CachedVolumes)
	{
		if (AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (Volume->ShouldExecutePendingRegenerate())
			{
				Volume->ExecutePendingRegenerate();
				RedrawViewports();
			}
		}
	}

	// Ghost mirror preview: redraw when Ctrl state changes so the ghost updates
	if (GhostMirrorAxisMask != 0 && GhostMirrorConnector.IsValid())
	{
		const bool bCtrlNow = FSlateApplication::Get().GetModifierKeys().IsControlDown();
		if (bCtrlNow != bGhostLastCtrlState)
		{
			bGhostLastCtrlState = bCtrlNow;
			RedrawViewports();
		}
	}
}

void UPCGExValencyCageEditorMode::CollectCagesFromLevel()
{
	CachedCages.Empty();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APCGExValencyCageBase> It(World); It; ++It)
		{
			CachedCages.Add(*It);
		}
	}
}

void UPCGExValencyCageEditorMode::CollectVolumesFromLevel()
{
	CachedVolumes.Empty();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AValencyContextVolume> It(World); It; ++It)
		{
			CachedVolumes.Add(*It);
		}
	}
}

void UPCGExValencyCageEditorMode::CollectPalettesFromLevel()
{
	CachedPalettes.Empty();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APCGExValencyAssetPalette> It(World); It; ++It)
		{
			CachedPalettes.Add(*It);
		}
	}
}

void UPCGExValencyCageEditorMode::RefreshAllCages()
{
	// Phase 1: Initialize all cages (orbitals setup, volume assignment)
	// This must happen before connection detection since connections depend on orbitals
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			// Skip null cages - they don't have orbitals
			if (Cage->IsNullCage())
			{
				continue;
			}

			// Refresh containing volumes (transient, not saved)
			Cage->RefreshContainingVolumes();

			// Always reinitialize orbitals to ensure clean state
			Cage->InitializeOrbitalsFromSet();
		}
	}

	// Phase 2: Detect connections for all cages
	// Done as a separate pass so all cages have their orbitals ready
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			// Skip null cages - they don't detect connections
			if (Cage->IsNullCage())
			{
				continue;
			}

			Cage->DetectNearbyConnections();
		}
	}

	// Phase 3: Scan contained assets for all cages and palettes
	// Must happen before ghost mesh refresh so mirrored content is available
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			if (Cage->bAutoRegisterContainedAssets)
			{
				Cage->ScanAndRegisterContainedAssets();
			}
		}
	}

	for (const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr : CachedPalettes)
	{
		if (APCGExValencyAssetPalette* Palette = PalettePtr.Get())
		{
			if (Palette->bAutoRegisterContainedAssets)
			{
				Palette->ScanAndRegisterContainedAssets();
			}
		}
	}

	// Phase 4: Refresh ghost meshes for all cages
	// Done after all cages have their scanned content available
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* CageBase = CagePtr.Get())
		{
			CageBase->RefreshGhostMeshes();
		}
	}
}

void UPCGExValencyCageEditorMode::InitializeCage(APCGExValencyCageBase* Cage)
{
	if (!Cage || Cage->IsNullCage())
	{
		return;
	}

	// Setup volumes and orbitals
	Cage->RefreshContainingVolumes();
	Cage->InitializeOrbitalsFromSet();

	// Detect connections (uses virtual filter - pattern cages only connect to pattern cages)
	Cage->DetectNearbyConnections();

	// Scan contained assets for regular cages
	if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
	{
		if (RegularCage->bAutoRegisterContainedAssets)
		{
			RegularCage->ScanAndRegisterContainedAssets();
		}
	}

	// Refresh ghost meshes (virtual - dispatches to correct subclass)
	Cage->RefreshGhostMeshes();
}

void UPCGExValencyCageEditorMode::OnLevelActorAdded(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// Check if it's a cage
	if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Actor))
	{
		// Add to cache and initialize
		CachedCages.Add(Cage);
		InitializeCage(Cage);

		// New cage might be a connection target for existing cages - refresh all connections
		// (ShouldConsiderCageForConnection filter ensures pattern cages only connect to pattern cages)
		for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
		{
			if (APCGExValencyCageBase* OtherCage = CagePtr.Get())
			{
				if (OtherCage != Cage)
				{
					OtherCage->DetectNearbyConnections();
				}
			}
		}

		// Rebuild dependency graph - new cage may have MirrorSources
		ReferenceTracker.RebuildDependencyGraph();

		// Notify scene changed
		OnSceneChanged.Broadcast();

		// CRITICAL: Mark the new cage's containing volumes dirty to trigger rebuild
		// This ensures the build includes the newly added cage
		for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : Cage->GetContainingVolumes())
		{
			if (AValencyContextVolume* Volume = VolumePtr.Get())
			{
				DirtyStateManager.MarkVolumeDirty(Volume, EValencyDirtyFlags::Structure);
			}
		}
	}
	// Check if it's a volume
	else if (AValencyContextVolume* Volume = Cast<AValencyContextVolume>(Actor))
	{
		CachedVolumes.Add(Volume);
		bCacheDirty = true; // Volumes affect cage orbital sets - full refresh needed
		OnSceneChanged.Broadcast();
	}
	// Check if it's an asset palette
	else if (APCGExValencyAssetPalette* Palette = Cast<APCGExValencyAssetPalette>(Actor))
	{
		CachedPalettes.Add(Palette);
		// Palettes can be MirrorSources - rebuild dependency graph
		ReferenceTracker.RebuildDependencyGraph();
		OnSceneChanged.Broadcast();
	}
}

void UPCGExValencyCageEditorMode::OnLevelActorDeleted(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// Check if it's a cage being deleted
	if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Actor))
	{
		// CRITICAL: Capture containing volumes BEFORE removing from cache
		// We need to mark these dirty to trigger a rebuild without the deleted cage
		TArray<AValencyContextVolume*> AffectedVolumes;
		for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : Cage->GetContainingVolumes())
		{
			if (AValencyContextVolume* Volume = VolumePtr.Get())
			{
				AffectedVolumes.Add(Volume);
			}
		}

		// Capture dependents BEFORE rebuilding the dependency graph
		// Cages that mirrored/proxied the deleted cage need their ghost meshes refreshed
		TArray<AActor*> Dependents;
		ReferenceTracker.CollectAffectedActors(Actor, Dependents);

		// Remove from cache
		CachedCages.RemoveAll([Cage](const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr)
		{
			return CagePtr.Get() == Cage || !CagePtr.IsValid();
		});

		// Rebuild dependency graph - removed cage may have been a dependency
		ReferenceTracker.RebuildDependencyGraph();

		// Refresh ghost meshes on cages that depended on the deleted actor
		for (AActor* Dependent : Dependents)
		{
			ReferenceTracker.RefreshDependentVisuals(Dependent);
		}

		// Refresh connections on remaining cages
		// (ShouldConsiderCageForConnection filter ensures pattern cages only connect to pattern cages)
		for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
		{
			if (APCGExValencyCageBase* OtherCage = CagePtr.Get())
			{
				OtherCage->DetectNearbyConnections();
			}
		}

		// CRITICAL: Mark affected volumes dirty to trigger rebuild
		// This ensures the build reflects the cage removal
		for (AValencyContextVolume* Volume : AffectedVolumes)
		{
			DirtyStateManager.MarkVolumeDirty(Volume, EValencyDirtyFlags::Structure);
		}

		OnSceneChanged.Broadcast();
	}
	// Check if it's a volume
	else if (AValencyContextVolume* Volume = Cast<AValencyContextVolume>(Actor))
	{
		CachedVolumes.RemoveAll([Volume](const TWeakObjectPtr<AValencyContextVolume>& VolumePtr)
		{
			return VolumePtr.Get() == Volume || !VolumePtr.IsValid();
		});
		bCacheDirty = true; // Volumes affect cage orbital sets - full refresh needed
		OnSceneChanged.Broadcast();
	}
	// Check if it's an asset palette
	else if (APCGExValencyAssetPalette* Palette = Cast<APCGExValencyAssetPalette>(Actor))
	{
		// Capture dependents BEFORE rebuilding the dependency graph
		TArray<AActor*> Dependents;
		ReferenceTracker.CollectAffectedActors(Actor, Dependents);

		CachedPalettes.RemoveAll([Palette](const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr)
		{
			return PalettePtr.Get() == Palette || !PalettePtr.IsValid();
		});

		// Rebuild dependency graph - removed palette may have been a dependency
		ReferenceTracker.RebuildDependencyGraph();

		// Refresh ghost meshes on cages that depended on the deleted palette
		for (AActor* Dependent : Dependents)
		{
			ReferenceTracker.RefreshDependentVisuals(Dependent);
		}

		OnSceneChanged.Broadcast();
	}
	// Check if it's a tracked asset actor being deleted
	else
	{
		APCGExValencyCage* AffectedCage = nullptr;
		if (AssetTracker.OnActorDeleted(Actor, AffectedCage))
		{
			if (AffectedCage)
			{
				DirtyStateManager.MarkCageDirty(AffectedCage, EValencyDirtyFlags::Assets);
			}
		}
	}
}

void UPCGExValencyCageEditorMode::OnSelectionChanged()
{
	// If a connector component is still selected but its owner cage was deselected,
	// clear the stale component selection to fully clean up visualizer state
	if (GEditor)
	{
		if (UPCGExValencyCageConnectorComponent* Connector = GetSelectedConnector())
		{
			AActor* OwnerActor = Connector->GetOwner();
			if (!OwnerActor || !GEditor->GetSelectedActors()->IsSelected(OwnerActor))
			{
				GEditor->SelectComponent(Connector, false, true);
			}
		}
	}

	AssetTracker.OnSelectionChanged();

	// Immediately check containment for newly selected actors
	if (AssetTracker.IsEnabled() && AssetTracker.GetTrackedActorCount() > 0)
	{
		TSet<APCGExValencyCage*> AffectedCages;
		TSet<APCGExValencyAssetPalette*> AffectedPalettes;
		if (AssetTracker.Update(AffectedCages, AffectedPalettes))
		{
			// Mark affected cages dirty - will be processed in next Tick
			for (APCGExValencyCage* Cage : AffectedCages)
			{
				if (Cage)
				{
					DirtyStateManager.MarkCageDirty(Cage, EValencyDirtyFlags::Assets);
				}
			}
			// Mark affected palettes dirty
			for (APCGExValencyAssetPalette* Palette : AffectedPalettes)
			{
				if (Palette)
				{
					DirtyStateManager.MarkPaletteDirty(Palette, EValencyDirtyFlags::Assets);
				}
			}
		}
	}
}

void UPCGExValencyCageEditorMode::OnPostUndoRedo()
{
	// After Undo/Redo, the level state may have changed in ways that bypass OnLevelActorAdded/Deleted.
	// We need to:
	// 1. Re-collect cages (some may have been added/removed by undo)
	// 2. Refresh all cage connections
	// 3. Mark all volumes dirty to trigger rebuild

	// Re-collect from level to catch any added/removed actors
	CollectCagesFromLevel();
	CollectVolumesFromLevel();
	CollectPalettesFromLevel();

	// Rebuild dependency graph
	ReferenceTracker.RebuildDependencyGraph();

	// Refresh ALL cage connections - undo/redo can affect any cage
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			if (!Cage->IsNullCage())
			{
				Cage->RefreshContainingVolumes();
				Cage->DetectNearbyConnections();
			}
		}
	}

	// Mark ALL volumes dirty to trigger rebuild with fresh data
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : CachedVolumes)
	{
		if (AValencyContextVolume* Volume = VolumePtr.Get())
		{
			DirtyStateManager.MarkVolumeDirty(Volume, EValencyDirtyFlags::Structure);
		}
	}

	OnSceneChanged.Broadcast();
	RedrawViewports();
}

void UPCGExValencyCageEditorMode::SetAllCageDebugComponentsVisible(bool bVisible)
{
	if (CachedCages.Num() == 0)
	{
		CollectCagesFromLevel();
	}

	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			Cage->SetDebugComponentsVisible(bVisible);
		}
	}
}

int32 UPCGExValencyCageEditorMode::CleanupAllManualConnections()
{
	int32 TotalRemoved = 0;

	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : CachedCages)
	{
		if (APCGExValencyCageBase* Cage = CagePtr.Get())
		{
			TotalRemoved += Cage->CleanupManualConnections();
		}
	}

	if (TotalRemoved > 0)
	{
		RedrawViewports();
	}

	return TotalRemoved;
}

void UPCGExValencyCageEditorMode::RedrawViewports()
{
	if (GEditor)
	{
		GEditor->RedrawAllViewports();

		for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (ViewportClient)
			{
				ViewportClient->Invalidate();
			}
		}
	}
}

// ========== Mirror Ghost Preview ==========

void UPCGExValencyCageEditorMode::SetMirrorGhostPreview(UPCGExValencyCageConnectorComponent* Connector, int32 AxisMask)
{
	GhostMirrorAxisMask = AxisMask;
	GhostMirrorConnector = Connector;
	bGhostLastCtrlState = FSlateApplication::Get().GetModifierKeys().IsControlDown();
	RedrawViewports();
}

void UPCGExValencyCageEditorMode::ClearMirrorGhostPreview()
{
	GhostMirrorAxisMask = 0;
	GhostMirrorConnector = nullptr;
	RedrawViewports();
}

// ========== Widget Interface ==========

bool UPCGExValencyCageEditorMode::UsesTransformWidget() const
{
	return true;
}

bool UPCGExValencyCageEditorMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return true;
}

bool UPCGExValencyCageEditorMode::ShouldDrawWidget() const
{
	if (GetSelectedConnector())
	{
		return true;
	}

	return GEditor && GEditor->GetSelectedActors()->Num() > 0;
}

// ========== Connector Management ==========

UPCGExValencyCageConnectorComponent* UPCGExValencyCageEditorMode::GetSelectedConnector()
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (USelection* CompSelection = GEditor->GetSelectedComponents())
	{
		for (FSelectionIterator It(*CompSelection); It; ++It)
		{
			if (UPCGExValencyCageConnectorComponent* Connector = Cast<UPCGExValencyCageConnectorComponent>(*It))
			{
				return Connector;
			}
		}
	}

	return nullptr;
}

APCGExValencyCageBase* UPCGExValencyCageEditorMode::GetSelectedCage()
{
	if (!GEditor)
	{
		return nullptr;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(*It))
		{
			return Cage;
		}
	}

	return nullptr;
}

UPCGExValencyCageConnectorComponent* UPCGExValencyCageEditorMode::AddConnectorToCage(APCGExValencyCageBase* Cage)
{
	if (!Cage || !GEditor)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "AddConnector", "Add Connector"));
	Cage->Modify();

	UPCGExValencyCageConnectorComponent* NewConnector = NewObject<UPCGExValencyCageConnectorComponent>(Cage, NAME_None, RF_Transactional);
	NewConnector->CreationMethod = EComponentCreationMethod::Instance;
	NewConnector->AttachToComponent(Cage->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewConnector->RegisterComponent();
	Cage->AddInstanceComponent(NewConnector);

	// Select the new connector
	GEditor->SelectActor(Cage, true, true);
	GEditor->SelectComponent(NewConnector, true, true);

	// Trigger rebuild and refresh
	Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
	OnSceneChanged.Broadcast();
	RedrawViewports();

	return NewConnector;
}

void UPCGExValencyCageEditorMode::RemoveConnector(UPCGExValencyCageConnectorComponent* Connector)
{
	if (!Connector || !GEditor)
	{
		return;
	}

	// Protect Blueprint-defined connectors from deletion on instances
	if (Connector->CreationMethod != EComponentCreationMethod::Instance)
	{
		return;
	}

	APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Connector->GetOwner());
	if (!Cage)
	{
		return;
	}

	FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "RemoveConnector", "Remove Connector"));
	Cage->Modify();
	Connector->Modify();

	// Clear selection before destroy
	GEditor->SelectComponent(Connector, false, true);

	Cage->RemoveInstanceComponent(Connector);
	Connector->DestroyComponent();

	// Trigger rebuild and refresh
	Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
	OnSceneChanged.Broadcast();
	RedrawViewports();
}

UPCGExValencyCageConnectorComponent* UPCGExValencyCageEditorMode::DuplicateConnector(UPCGExValencyCageConnectorComponent* Connector)
{
	if (!Connector || !GEditor)
	{
		return nullptr;
	}

	APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Connector->GetOwner());
	if (!Cage)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "DuplicateConnector", "Duplicate Connector"));
	Cage->Modify();

	UPCGExValencyCageConnectorComponent* NewConnector = NewObject<UPCGExValencyCageConnectorComponent>(Cage, NAME_None, RF_Transactional);
	NewConnector->CreationMethod = EComponentCreationMethod::Instance;
	NewConnector->AttachToComponent(Cage->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewConnector->RegisterComponent();
	Cage->AddInstanceComponent(NewConnector);

	// Copy properties from source
	NewConnector->ConnectorType = Connector->ConnectorType;
	NewConnector->Polarity = Connector->Polarity;
	NewConnector->bEnabled = Connector->bEnabled;
	NewConnector->DebugColorOverride = Connector->DebugColorOverride;

	// Offset the duplicate slightly in local X
	FTransform SourceTransform = Connector->GetRelativeTransform();
	SourceTransform.AddToTranslation(FVector(20.0, 0.0, 0.0));
	NewConnector->SetRelativeTransform(SourceTransform);

	// Deselect the original connector, then select the new one
	GEditor->SelectComponent(Connector, false, false);
	GEditor->SelectActor(Cage, true, true);
	GEditor->SelectComponent(NewConnector, true, true);

	// Trigger rebuild and refresh
	Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
	OnSceneChanged.Broadcast();
	RedrawViewports();

	return NewConnector;
}

// ========== Connector Command Execute/CanExecute ==========

void UPCGExValencyCageEditorMode::ExecuteAddConnector()
{
	if (APCGExValencyCageBase* Cage = GetSelectedCage())
	{
		AddConnectorToCage(Cage);
	}
}

bool UPCGExValencyCageEditorMode::CanExecuteAddConnector() const
{
	return GetSelectedCage() != nullptr;
}

void UPCGExValencyCageEditorMode::ExecuteRemoveConnector()
{
	if (UPCGExValencyCageConnectorComponent* Connector = GetSelectedConnector())
	{
		RemoveConnector(Connector);
	}
}

bool UPCGExValencyCageEditorMode::CanExecuteRemoveConnector() const
{
	UPCGExValencyCageConnectorComponent* Connector = GetSelectedConnector();
	if (!Connector) return false;
	return Connector->CreationMethod == EComponentCreationMethod::Instance;
}

void UPCGExValencyCageEditorMode::ExecuteDuplicateConnector()
{
	if (UPCGExValencyCageConnectorComponent* Connector = GetSelectedConnector())
	{
		DuplicateConnector(Connector);
	}
}

bool UPCGExValencyCageEditorMode::CanExecuteDuplicateConnector() const
{
	return GetSelectedConnector() != nullptr;
}

void UPCGExValencyCageEditorMode::ExecuteCycleConnectorPolarity()
{
	UPCGExValencyCageConnectorComponent* Connector = GetSelectedConnector();
	if (!Connector)
	{
		return;
	}

	FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "CycleConnectorPolarity", "Cycle Connector Polarity"));
	Connector->Modify();

	// Cycle polarity: Universal -> Plug -> Port -> Universal
	switch (Connector->Polarity)
	{
	case EPCGExConnectorPolarity::Universal: Connector->Polarity = EPCGExConnectorPolarity::Plug; break;
	case EPCGExConnectorPolarity::Plug: Connector->Polarity = EPCGExConnectorPolarity::Port; break;
	case EPCGExConnectorPolarity::Port: Connector->Polarity = EPCGExConnectorPolarity::Universal; break;
	}

	if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
	{
		Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
	}

	OnSceneChanged.Broadcast();
	RedrawViewports();
}

bool UPCGExValencyCageEditorMode::CanExecuteCycleConnectorPolarity() const
{
	return GetSelectedConnector() != nullptr;
}
