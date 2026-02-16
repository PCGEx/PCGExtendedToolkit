// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Cages/PCGExValencyCageBase.h"

#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "EngineUtils.h"
#include "PCGExValencyEditorCommon.h"
#include "PCGExValencyMacros.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExValencyLog.h"
#include "Volumes/ValencyContextVolume.h"
#include "Cages/PCGExValencyCageSpatialRegistry.h"
#include "EditorMode/PCGExValencyDirtyState.h"

APCGExValencyCageBase::APCGExValencyCageBase()
{
	// Configure as editor-only
	bNetLoadOnClient = false;
	bReplicates = false;

	// Default root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void APCGExValencyCageBase::PostActorCreated()
{
	Super::PostActorCreated();

	// Mark as newly created (not loaded from disk)
	bIsNewlyCreated = true;

	// Auto-organize into Valency/Cages folder
	SetFolderPath(PCGExValencyFolders::CagesFolder);
}

void APCGExValencyCageBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Register with spatial registry
	if (UWorld* World = GetWorld())
	{
		FPCGExValencyCageSpatialRegistry::Get(World).RegisterCage(this);
	}

	// Initial setup
	RefreshContainingVolumes();

	if (bNeedsOrbitalInit)
	{
		InitializeOrbitalsFromSet();
		bNeedsOrbitalInit = false;
	}

	// Initialize drag tracking
	LastDragUpdatePosition = GetActorLocation();

	// If this is a newly created cage (not loaded), trigger auto-rebuild for containing volumes
	if (bIsNewlyCreated)
	{
		bIsNewlyCreated = false; // Clear flag
		RequestRebuild(EValencyRebuildReason::AssetChange);
	}
}

void APCGExValencyCageBase::OnGhostRefreshRequested()
{
	ClearGhostMeshes();
	RefreshGhostMeshes();
}

void APCGExValencyCageBase::OnRebuildMetaTagTriggered()
{
	RequestRebuild(EValencyRebuildReason::PropertyChange);
}

void APCGExValencyCageBase::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyCageBase, ProbeRadius))
	{
		// Probe radius changed - redetect connections
		// This can change which cages are connected, so trigger rebuild if connections changed
		if (DetectNearbyConnections())
		{
			RequestRebuild(EValencyRebuildReason::ConnectionChange);
		}

		PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyCageBase, CageName))
	{
		// Display name changed - just redraw, no rebuild needed
		PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyCageBase, bTransformOrbitalDirections))
	{
		// Transform settings changed - redetect connections and trigger rebuild
		// This changes how orbital directions are computed, affecting connections
		DetectNearbyConnections();
		RequestRebuild(EValencyRebuildReason::ConnectionChange);

		PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
	}
}

void APCGExValencyCageBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	// Clear orphaned ghost mesh components from duplication and refresh from current state
	ClearGhostMeshes();
	RefreshGhostMeshes();
}

void APCGExValencyCageBase::BeginDestroy()
{
	// Clean up ghost mesh components before destruction
	ClearGhostMeshes();

	// Unregister from spatial registry and trigger auto-rebuild if this is a user deletion
	if (UWorld* World = GetWorld())
	{
		FPCGExValencyCageSpatialRegistry::Get(World).UnregisterCage(this);

		// Only trigger auto-rebuild if world is valid and not being torn down
#if WITH_EDITOR
		if (!World->bIsTearingDown && !World->IsPlayInEditor() && ContainingVolumes.Num() > 0)
		{
			// IMPORTANT: We can't use RequestRebuild(this) here because by the time ProcessDirty
			// runs (next tick), this cage will already be destroyed and the weak pointer invalid.
			// Instead, mark the containing VOLUMES dirty directly - they will persist and trigger rebuild.
			if (FValencyDirtyStateManager* Manager = GetActiveDirtyStateManager())
			{
				for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
				{
					if (AValencyContextVolume* Volume = VolumePtr.Get())
					{
						Manager->MarkVolumeDirty(Volume, EValencyDirtyFlags::Structure);
						PCGEX_VALENCY_VERBOSE(Rebuild, "Cage '%s' being destroyed - marked volume dirty for rebuild", *GetCageDisplayName());
					}
				}
			}
			else
			{
				// Fallback: trigger direct rebuild since dirty state manager isn't available
				TriggerAutoRebuildIfNeeded();
			}
		}
#endif
	}

	Super::BeginDestroy();
}

void APCGExValencyCageBase::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	const FVector CurrentPosition = GetActorLocation();

	if (!bFinished)
	{
		// Continuous drag - track state and do throttled live updates
		if (!bIsDragging)
		{
			// Drag just started - capture current state BEFORE any updates
			bIsDragging = true;
			DragStartPosition = LastDragUpdatePosition;
			VolumesBeforeDrag = ContainingVolumes; // Capture volume membership before drag
			CaptureConnectionState(ConnectionsBeforeDrag); // Capture connections before drag
		}

		// Check if we've moved enough to warrant an update
		const float DistanceMoved = FVector::Dist(CurrentPosition, LastDragUpdatePosition);
		if (DistanceMoved >= DragUpdateThreshold)
		{
			UpdateConnectionsDuringDrag();
			LastDragUpdatePosition = CurrentPosition;
		}
	}
	else
	{
		// Drag finished - do final update
		const bool bWasDragging = bIsDragging;
		bIsDragging = false;

		// Update spatial registry with final position
		if (UWorld* World = GetWorld())
		{
			FPCGExValencyCageSpatialRegistry::Get(World).UpdateCagePosition(this, DragStartPosition, CurrentPosition);
		}

		// Capture old state before refresh (only valid if we had a proper drag sequence)
		TArray<TWeakObjectPtr<AValencyContextVolume>> OldVolumes = VolumesBeforeDrag;
		TArray<TWeakObjectPtr<APCGExValencyCageBase>> OldConnections = ConnectionsBeforeDrag;

		// Position changed - refresh volumes and connections
		RefreshContainingVolumes();
		DetectNearbyConnections(); // Update to final state

		// Compare connections with drag start state (not the intermediate drag state)
		const bool bConnectionsChanged = bWasDragging && HaveConnectionsChanged(OldConnections);

		// Check for volume membership changes and trigger auto-rebuild if needed
		// Only do this if we properly captured the "before" state during drag start
		if (bWasDragging)
		{
			HandleVolumeMembershipChange(OldVolumes);
		}

		// Trigger rebuild if this cage's connections changed during the entire drag operation
		if (bConnectionsChanged)
		{
			RequestRebuild(EValencyRebuildReason::ConnectionChange);
		}

		// Notify affected cages so they can update their connections
		NotifyAffectedCagesOfMovement(DragStartPosition, CurrentPosition);

		LastDragUpdatePosition = CurrentPosition;
		VolumesBeforeDrag.Empty(); // Clear after use
		ConnectionsBeforeDrag.Empty(); // Clear after use
	}
}

FString APCGExValencyCageBase::GetCageDisplayName() const
{
	if (!CageName.IsEmpty())
	{
		return CageName;
	}

	return GetActorNameOrLabel();
}

UPCGExValencyOrbitalSet* APCGExValencyCageBase::GetEffectiveOrbitalSet() const
{
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (UPCGExValencyOrbitalSet* Set = Volume->GetEffectiveOrbitalSet())
			{
				return Set;
			}
		}
	}

	return nullptr;
}

UPCGExValencyBondingRules* APCGExValencyCageBase::GetEffectiveBondingRules() const
{
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (UPCGExValencyBondingRules* Rules = Volume->GetBondingRules())
			{
				return Rules;
			}
		}
	}

	return nullptr;
}

float APCGExValencyCageBase::GetEffectiveProbeRadius() const
{
	float BaseRadius;

	// Explicit override
	if (ProbeRadius >= 0.0f)
	{
		BaseRadius = ProbeRadius;
	}
	else
	{
		// Get from first containing volume
		BaseRadius = 100.0f; // Default fallback
		for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
		{
			if (const AValencyContextVolume* Volume = VolumePtr.Get())
			{
				BaseRadius = Volume->GetDefaultProbeRadius();
				break;
			}
		}
	}

	return BaseRadius;
}

bool APCGExValencyCageBase::ShouldTransformOrbitalDirections() const
{
	return bTransformOrbitalDirections;
}

bool APCGExValencyCageBase::HasConnectionTo(const APCGExValencyCageBase* OtherCage) const
{
	if (!OtherCage)
	{
		return false;
	}

	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (!Orbital.bEnabled)
		{
			continue;
		}

		// Check manual connections list
		if (Orbital.IsManualTarget(OtherCage))
		{
			return true;
		}

		// Check auto connection
		if (Orbital.AutoConnectedCage.Get() == OtherCage)
		{
			return true;
		}
	}

	return false;
}

int32 APCGExValencyCageBase::GetOrbitalIndexTo(const APCGExValencyCageBase* OtherCage) const
{
	if (!OtherCage)
	{
		return -1;
	}

	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (!Orbital.bEnabled)
		{
			continue;
		}

		// Check manual connections list
		if (Orbital.IsManualTarget(OtherCage))
		{
			return Orbital.OrbitalIndex;
		}

		// Check auto connection
		if (Orbital.AutoConnectedCage.Get() == OtherCage)
		{
			return Orbital.OrbitalIndex;
		}
	}

	return -1;
}

void APCGExValencyCageBase::OnContainingVolumeChanged(AValencyContextVolume* Volume)
{
	// Refresh our state when a containing volume changes
	RefreshContainingVolumes();

	// If orbital set changed, reinitialize
	UPCGExValencyOrbitalSet* NewSet = GetEffectiveOrbitalSet();
	if (NewSet != CachedOrbitalSet.Get())
	{
		CachedOrbitalSet = NewSet;
		InitializeOrbitalsFromSet();
	}
}

void APCGExValencyCageBase::RefreshContainingVolumes()
{
	ContainingVolumes.Empty();

	if (UWorld* World = GetWorld())
	{
		const FVector MyLocation = GetActorLocation();

		for (TActorIterator<AValencyContextVolume> It(World); It; ++It)
		{
			AValencyContextVolume* Volume = *It;
			if (Volume && Volume->ContainsPoint(MyLocation))
			{
				ContainingVolumes.Add(Volume);
			}
		}
	}
}

bool APCGExValencyCageBase::ShouldIgnoreActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return true;
	}

	// Check against all containing volumes' ignore rules
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (Volume->ShouldIgnoreActor(Actor))
			{
				return true;
			}
		}
	}

	return false;
}

void APCGExValencyCageBase::InitializeOrbitalsFromSet()
{
	const UPCGExValencyOrbitalSet* OrbitalSet = GetEffectiveOrbitalSet();
	if (!OrbitalSet)
	{
		// No orbital set - clear orbitals
		Orbitals.Empty();
		return;
	}

	// Cache the set
	CachedOrbitalSet = const_cast<UPCGExValencyOrbitalSet*>(OrbitalSet);

	// Preserve existing data where possible
	TMap<int32, TArray<TObjectPtr<APCGExValencyCageBase>>> ExistingManualConnections;
	TMap<int32, TWeakObjectPtr<APCGExValencyCageBase>> ExistingAutoConnections;
	TMap<int32, bool> ExistingEnabled;

	for (const FPCGExValencyCageOrbital& Existing : Orbitals)
	{
		if (Existing.OrbitalIndex >= 0)
		{
			ExistingManualConnections.Add(Existing.OrbitalIndex, Existing.ManualConnections);
			ExistingAutoConnections.Add(Existing.OrbitalIndex, Existing.AutoConnectedCage);
			ExistingEnabled.Add(Existing.OrbitalIndex, Existing.bEnabled);
		}
	}

	// Rebuild orbitals array from OrbitalSet
	Orbitals.Empty(OrbitalSet->Num());

	for (int32 i = 0; i < OrbitalSet->Num(); ++i)
	{
		const FPCGExValencyOrbitalEntry& Entry = OrbitalSet->Orbitals[i];

		FPCGExValencyCageOrbital& Orbital = Orbitals.AddDefaulted_GetRef();
		Orbital.OrbitalIndex = i;
		Orbital.OrbitalName = Entry.GetOrbitalName();

		// Restore existing manual connections if any (user-defined, persisted)
		if (TArray<TObjectPtr<APCGExValencyCageBase>>* ManualPtr = ExistingManualConnections.Find(i))
		{
			Orbital.ManualConnections = *ManualPtr;
		}
		// Restore existing auto connection if any (will be refreshed by DetectNearbyConnections)
		if (TWeakObjectPtr<APCGExValencyCageBase>* AutoPtr = ExistingAutoConnections.Find(i))
		{
			Orbital.AutoConnectedCage = *AutoPtr;
		}
		if (bool* EnabledPtr = ExistingEnabled.Find(i))
		{
			Orbital.bEnabled = *EnabledPtr;
		}
	}
}

bool APCGExValencyCageBase::DetectNearbyConnections()
{
	const float Radius = GetEffectiveProbeRadius();
	if (Radius <= 0.0f)
	{
		// Radius 0 = receive-only, don't detect
		return false;
	}

	const UPCGExValencyOrbitalSet* OrbitalSet = GetEffectiveOrbitalSet();
	if (!OrbitalSet || OrbitalSet->Num() == 0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector MyLocation = GetActorLocation();
	const FTransform MyTransform = GetActorTransform();

	// Build orbital cache for direction matching
	PCGExValency::FOrbitalDirectionResolver OrbitalResolver;
	if (!OrbitalResolver.BuildFrom(OrbitalSet))
	{
		return false;
	}

	// Build set of cages that are manual targets (excluded from auto-detection)
	TSet<const APCGExValencyCageBase*> ManualTargets;
	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		for (const TObjectPtr<APCGExValencyCageBase>& ManualCage : Orbital.ManualConnections)
		{
			if (ManualCage)
			{
				ManualTargets.Add(ManualCage);
			}
		}
	}

	// Capture old connections for change detection
	TArray<TWeakObjectPtr<APCGExValencyCageBase>> OldConnections;
	OldConnections.Reserve(Orbitals.Num());
	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		OldConnections.Add(Orbital.AutoConnectedCage);
	}

	// Clear existing auto-connections (manual connections are preserved)
	for (FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		Orbital.AutoConnectedCage = nullptr;
	}

	// Find nearby cages
	for (TActorIterator<APCGExValencyCageBase> It(World); It; ++It)
	{
		APCGExValencyCageBase* OtherCage = *It;
		if (OtherCage == this)
		{
			continue;
		}

		// Skip cages that are manual targets - they're handled separately
		if (ManualTargets.Contains(OtherCage))
		{
			continue;
		}

		// Virtual filter - allows subclasses to restrict connection targets
		if (!ShouldConsiderCageForConnection(OtherCage))
		{
			continue;
		}

		const FVector OtherLocation = OtherCage->GetActorLocation();
		const float Distance = FVector::Dist(MyLocation, OtherLocation);

		if (Distance > Radius)
		{
			continue;
		}

		// Check direction to other cage
		const FVector Direction = (OtherLocation - MyLocation).GetSafeNormal();

		// Find matching orbital (use per-cage transform setting)
		const uint8 OrbitalIndex = OrbitalResolver.FindMatchingOrbital(
			Direction,
			ShouldTransformOrbitalDirections(),
			MyTransform
		);

		if (OrbitalIndex != PCGExValency::NO_ORBITAL_MATCH && Orbitals.IsValidIndex(OrbitalIndex))
		{
			FPCGExValencyCageOrbital& Orbital = Orbitals[OrbitalIndex];
			if (!Orbital.AutoConnectedCage.IsValid())
			{
				// Connect to closest cage in this direction (auto-detected)
				Orbital.AutoConnectedCage = OtherCage;
			}
		}
	}

	// Check if connections changed
	for (int32 i = 0; i < Orbitals.Num(); ++i)
	{
		if (OldConnections[i].Get() != Orbitals[i].AutoConnectedCage.Get())
		{
			return true;
		}
	}
	return false;
}

int32 APCGExValencyCageBase::CleanupManualConnections()
{
	int32 TotalRemoved = 0;

	for (FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		TotalRemoved += Orbital.CleanupManualConnections();
	}

	if (TotalRemoved > 0)
	{
		Modify(); // Mark as needing save
	}

	return TotalRemoved;
}

void APCGExValencyCageBase::OnRelatedCageMoved(APCGExValencyCageBase* MovedCage)
{
	if (!MovedCage || MovedCage == this)
	{
		return;
	}

	// Refresh connections and trigger rebuild only if connections actually changed
	if (DetectNearbyConnections())
	{
		RequestRebuild(EValencyRebuildReason::ConnectionChange);
	}
}

void APCGExValencyCageBase::NotifyAllCagesOfMovement()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Notify all other cages that we moved
	for (TActorIterator<APCGExValencyCageBase> It(World); It; ++It)
	{
		APCGExValencyCageBase* OtherCage = *It;
		if (OtherCage && OtherCage != this)
		{
			OtherCage->OnRelatedCageMoved(this);
		}
	}

	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
}

void APCGExValencyCageBase::SetDebugComponentsVisible(bool bVisible)
{
	// Base implementation does nothing - subclasses override to hide their specific components
}

void APCGExValencyCageBase::RefreshGhostMeshes()
{
	// Base implementation does nothing - subclasses override to create ghost previews
}

void APCGExValencyCageBase::ClearGhostMeshes()
{
	// Tag-based cleanup: finds all ghost components including orphaned ones from duplication
	TArray<UActorComponent*> AllComponents;
	GetComponents(AllComponents);

	for (UActorComponent* Component : AllComponents)
	{
		if (Component && Component->ComponentHasTag(PCGExValencyTags::GhostMeshTag))
		{
			Component->DestroyComponent();
		}
	}
}

void APCGExValencyCageBase::CaptureConnectionState(TArray<TWeakObjectPtr<APCGExValencyCageBase>>& OutConnections) const
{
	OutConnections.Empty(Orbitals.Num());
	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		OutConnections.Add(Orbital.AutoConnectedCage);
	}
}

bool APCGExValencyCageBase::HaveConnectionsChanged(const TArray<TWeakObjectPtr<APCGExValencyCageBase>>& OldConnections) const
{
	// If orbital count changed, connections definitely changed
	if (OldConnections.Num() != Orbitals.Num())
	{
		return true;
	}

	// Compare each orbital's auto-connection
	for (int32 i = 0; i < Orbitals.Num(); ++i)
	{
		if (OldConnections[i].Get() != Orbitals[i].AutoConnectedCage.Get())
		{
			return true;
		}
	}

	return false;
}

void APCGExValencyCageBase::UpdateConnectionsDuringDrag()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector CurrentPosition = GetActorLocation();
	FPCGExValencyCageSpatialRegistry& Registry = FPCGExValencyCageSpatialRegistry::Get(World);

	// Find cages that might be affected by our movement
	TSet<APCGExValencyCageBase*> AffectedCages;
	Registry.FindAffectedCages(this, LastDragUpdatePosition, CurrentPosition, AffectedCages);

	// Update our own connections
	DetectNearbyConnections();

	// Update affected cages' connections
	for (APCGExValencyCageBase* Cage : AffectedCages)
	{
		if (Cage)
		{
			Cage->DetectNearbyConnections();
		}
	}

	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
}

void APCGExValencyCageBase::NotifyAffectedCagesOfMovement(const FVector& OldPosition, const FVector& NewPosition)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FPCGExValencyCageSpatialRegistry& Registry = FPCGExValencyCageSpatialRegistry::Get(World);

	// Find all cages affected by our movement
	TSet<APCGExValencyCageBase*> AffectedCages;
	Registry.FindAffectedCages(this, OldPosition, NewPosition, AffectedCages);

	// Notify each affected cage
	for (APCGExValencyCageBase* Cage : AffectedCages)
	{
		if (Cage)
		{
			Cage->OnRelatedCageMoved(this);
		}
	}

	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
}

void APCGExValencyCageBase::HandleVolumeMembershipChange(const TArray<TWeakObjectPtr<AValencyContextVolume>>& OldVolumes)
{
	// Only process auto-rebuild when Valency mode is active
	if (!AValencyContextVolume::IsValencyModeActive())
	{
		return;
	}

	// Build sets for comparison
	TSet<AValencyContextVolume*> OldVolumeSet;
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : OldVolumes)
	{
		if (AValencyContextVolume* Volume = VolumePtr.Get())
		{
			OldVolumeSet.Add(Volume);
		}
	}

	TSet<AValencyContextVolume*> NewVolumeSet;
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (AValencyContextVolume* Volume = VolumePtr.Get())
		{
			NewVolumeSet.Add(Volume);
		}
	}

	// Find volumes affected by membership change
	TArray<AValencyContextVolume*> AffectedVolumes;

	// Volumes that lost this cage (was in old, not in new)
	for (AValencyContextVolume* OldVolume : OldVolumeSet)
	{
		if (!NewVolumeSet.Contains(OldVolume))
		{
			AffectedVolumes.AddUnique(OldVolume);
		}
	}

	// Volumes that gained this cage (in new, not in old)
	for (AValencyContextVolume* NewVolume : NewVolumeSet)
	{
		if (!OldVolumeSet.Contains(NewVolume))
		{
			AffectedVolumes.AddUnique(NewVolume);
		}
	}

	// Mark affected volumes dirty directly
	// This is critical for cages dragged OUT of volumes - the cage is no longer inside,
	// so CollectAffectedVolumes won't find it via containment check
	if (AffectedVolumes.Num() > 0)
	{
		if (FValencyDirtyStateManager* Manager = GetActiveDirtyStateManager())
		{
			for (AValencyContextVolume* Volume : AffectedVolumes)
			{
				Manager->MarkVolumeDirty(Volume, EValencyDirtyFlags::Structure);
			}
		}
		else
		{
			// Fallback: trigger direct rebuild
			TriggerAutoRebuildForVolumes(AffectedVolumes);
		}
	}
}

bool APCGExValencyCageBase::TriggerAutoRebuildIfNeeded()
{
	// Only process when Valency mode is active
	if (!AValencyContextVolume::IsValencyModeActive())
	{
		return false;
	}

	// Collect containing volumes
	TArray<AValencyContextVolume*> Volumes;
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (AValencyContextVolume* Volume = VolumePtr.Get())
		{
			Volumes.Add(Volume);
		}
	}

	return TriggerAutoRebuildForVolumes(Volumes);
}

bool APCGExValencyCageBase::TriggerAutoRebuildForVolumes(const TArray<AValencyContextVolume*>& Volumes)
{
	// Only process when Valency mode is active
	if (!AValencyContextVolume::IsValencyModeActive())
	{
		return false;
	}

	// Find first volume with auto-rebuild enabled and trigger it
	// Multi-volume aggregation handles rebuilding all related volumes
	for (AValencyContextVolume* Volume : Volumes)
	{
		if (Volume && Volume->bAutoRebuildOnChange)
		{
			Volume->BuildRulesFromCages();
			return true;
		}
	}
	return false;
}

void APCGExValencyCageBase::RequestRebuild(EValencyRebuildReason Reason)
{
	// Only process when Valency mode is active
	if (!AValencyContextVolume::IsValencyModeActive())
	{
		return;
	}

	// Get the dirty state manager from the active editor mode
	FValencyDirtyStateManager* Manager = GetActiveDirtyStateManager();
	if (!Manager)
	{
		// Fallback to direct rebuild if manager not available
		TriggerAutoRebuildIfNeeded();
		return;
	}

	// Convert reason to dirty flags
	EValencyDirtyFlags Flags = EValencyDirtyFlags::Structure; // Default
	switch (Reason)
	{
	case EValencyRebuildReason::PropertyChange:
		Flags = EValencyDirtyFlags::Structure;
		break;
	case EValencyRebuildReason::Movement:
		Flags = EValencyDirtyFlags::Transform | EValencyDirtyFlags::Orbitals;
		break;
	case EValencyRebuildReason::AssetChange:
		Flags = EValencyDirtyFlags::Assets;
		break;
	case EValencyRebuildReason::ConnectionChange:
		Flags = EValencyDirtyFlags::Orbitals;
		break;
	case EValencyRebuildReason::ExternalCascade:
		Flags = EValencyDirtyFlags::Structure;
		break;
	}

	// Mark this cage as dirty - ProcessDirty will handle the rest
	Manager->MarkCageDirty(this, Flags);

	// Log rebuild request with reason
	static const TCHAR* ReasonNames[] = {
		TEXT("PropertyChange"),
		TEXT("Movement"),
		TEXT("AssetChange"),
		TEXT("ConnectionChange"),
		TEXT("ExternalCascade")
	};
	const int32 ReasonIndex = static_cast<int32>(Reason);
	const TCHAR* ReasonName = (ReasonIndex >= 0 && ReasonIndex < UE_ARRAY_COUNT(ReasonNames))
		? ReasonNames[ReasonIndex]
		: TEXT("Unknown");
	PCGEX_VALENCY_VERBOSE(Rebuild, "RequestRebuild from '%s' (reason: %s)", *GetCageDisplayName(), ReasonName);
}

// ========== Connector Methods (Component-based) ==========

UPCGExValencyConnectorSet* APCGExValencyCageBase::GetEffectiveConnectorSet() const
{
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (UPCGExValencyConnectorSet* VolumeSet = Volume->GetEffectiveConnectorSet())
			{
				return VolumeSet;
			}
		}
	}

	if (const UPCGExValencyBondingRules* Rules = GetEffectiveBondingRules())
	{
		return Rules->ConnectorSet;
	}

	return nullptr;
}

void APCGExValencyCageBase::GetConnectorComponents(TArray<UPCGExValencyCageConnectorComponent*>& OutComponents) const
{
	// Use const_cast because GetComponents doesn't have a const overload that returns non-const pointers
	const_cast<APCGExValencyCageBase*>(this)->GetComponents<UPCGExValencyCageConnectorComponent>(OutComponents);
}

bool APCGExValencyCageBase::HasConnectors() const
{
	TArray<UPCGExValencyCageConnectorComponent*> Components;
	GetConnectorComponents(Components);

	for (const UPCGExValencyCageConnectorComponent* Comp : Components)
	{
		if (Comp && Comp->bEnabled)
		{
			return true;
		}
	}
	return false;
}

bool APCGExValencyCageBase::HasPlugConnectors() const
{
	TArray<UPCGExValencyCageConnectorComponent*> Components;
	GetConnectorComponents(Components);

	for (const UPCGExValencyCageConnectorComponent* Comp : Components)
	{
		if (Comp && Comp->bEnabled && Comp->Polarity == EPCGExConnectorPolarity::Plug)
		{
			return true;
		}
	}
	return false;
}

UPCGExValencyCageConnectorComponent* APCGExValencyCageBase::FindConnectorByIdentifier(const FName& Identifier) const
{
	TArray<UPCGExValencyCageConnectorComponent*> Components;
	GetConnectorComponents(Components);

	for (UPCGExValencyCageConnectorComponent* Comp : Components)
	{
		if (Comp && Comp->Identifier == Identifier)
		{
			return Comp;
		}
	}
	return nullptr;
}

UPCGExValencyCageConnectorComponent* APCGExValencyCageBase::FindConnectorByType(const FName& ConnectorType) const
{
	TArray<UPCGExValencyCageConnectorComponent*> Components;
	GetConnectorComponents(Components);

	for (UPCGExValencyCageConnectorComponent* Comp : Components)
	{
		if (Comp && Comp->ConnectorType == ConnectorType)
		{
			return Comp;
		}
	}
	return nullptr;
}

int32 APCGExValencyCageBase::CreateConnectorComponentsFromMesh(UStaticMesh* Mesh, const FName& DefaultConnectorType, EPCGExConnectorPolarity DefaultPolarity)
{
	if (!Mesh)
	{
		return 0;
	}

	int32 CreatedCount = 0;

	// Get all sockets from the static mesh
	const TArray<UStaticMeshSocket*>& MeshSockets = Mesh->Sockets;
	for (const UStaticMeshSocket* MeshSocket : MeshSockets)
	{
		if (!MeshSocket)
		{
			continue;
		}

		// Check if a connector component with this identifier already exists
		if (FindConnectorByIdentifier(MeshSocket->SocketName))
		{
			continue; // Skip duplicate
		}

		// Create a new connector component
		UPCGExValencyCageConnectorComponent* NewComponent = NewObject<UPCGExValencyCageConnectorComponent>(
			this,
			UPCGExValencyCageConnectorComponent::StaticClass(),
			MakeUniqueObjectName(this, UPCGExValencyCageConnectorComponent::StaticClass(), MeshSocket->SocketName)
		);

		if (!NewComponent)
		{
			continue;
		}

		// Configure the component
		NewComponent->Identifier = MeshSocket->SocketName;
		NewComponent->ConnectorType = DefaultConnectorType;
		NewComponent->Polarity = DefaultPolarity;
		NewComponent->bEnabled = true;
		NewComponent->MeshSocketName = MeshSocket->SocketName; // Link to mesh socket for sync

		// Set transform from mesh socket
		const FTransform SocketTransform(
			MeshSocket->RelativeRotation,
			MeshSocket->RelativeLocation,
			MeshSocket->RelativeScale
		);
		NewComponent->SetRelativeTransform(SocketTransform);

		// Attach to root component
		NewComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		NewComponent->RegisterComponent();

		CreatedCount++;
	}

	// Request rebuild if we created any connector components
	if (CreatedCount > 0)
	{
		RequestRebuild(EValencyRebuildReason::AssetChange);
	}

	return CreatedCount;
}
