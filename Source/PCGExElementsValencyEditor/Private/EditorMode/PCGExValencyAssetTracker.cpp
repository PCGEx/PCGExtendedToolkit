// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExValencyAssetTracker.h"

#include "Editor.h"
#include "Selection.h"

#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Volumes/ValencyContextVolume.h"

void FPCGExValencyAssetTracker::Initialize(
	const TArray<TWeakObjectPtr<APCGExValencyCageBase>>& InCachedCages,
	const TArray<TWeakObjectPtr<AValencyContextVolume>>& InCachedVolumes,
	const TArray<TWeakObjectPtr<APCGExValencyAssetPalette>>& InCachedPalettes)
{
	CachedCages = &InCachedCages;
	CachedVolumes = &InCachedVolumes;
	CachedPalettes = &InCachedPalettes;
}

void FPCGExValencyAssetTracker::Reset()
{
	TrackedActors.Empty();
	TrackedActorCageMap.Empty();
	TrackedActorPaletteMap.Empty();
	TrackedActorTransforms.Empty();
}

bool FPCGExValencyAssetTracker::IsEnabled() const
{
	if (!CachedVolumes)
	{
		return false;
	}

	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : *CachedVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (Volume->bAutoTrackAssetPlacement)
			{
				return true;
			}
		}
	}

	return false;
}

void FPCGExValencyAssetTracker::OnSelectionChanged()
{
	if (!IsEnabled())
	{
		return;
	}

	// Rebuild tracked actors list from current selection
	TrackedActors.Empty();

	if (!GEditor)
	{
		return;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		// Skip cages, volumes, and palettes - we only track potential asset actors
		if (Cast<APCGExValencyCageBase>(Actor) || Cast<AValencyContextVolume>(Actor) || Cast<APCGExValencyAssetPalette>(Actor))
		{
			continue;
		}

		// Skip actors that are ignored by any volume with tracking enabled
		if (ShouldIgnoreActor(Actor))
		{
			continue;
		}

		TrackedActors.Add(Actor);

		// NOTE: Do NOT initialize position here - let Update() handle it
		// so that bIsNewActor can be properly detected for initial containment check
	}
}

bool FPCGExValencyAssetTracker::OnActorDeleted(AActor* DeletedActor, APCGExValencyCage*& OutAffectedCage)
{
	OutAffectedCage = nullptr;

	if (!DeletedActor || !IsEnabled())
	{
		return false;
	}

	bool bAffectedAnything = false;

	// Check if this actor was in a tracked cage
	TWeakObjectPtr<APCGExValencyCage>* CagePtr = TrackedActorCageMap.Find(DeletedActor);
	if (CagePtr && CagePtr->IsValid())
	{
		APCGExValencyCage* ContainingCage = CagePtr->Get();

		// Refresh the cage's scanned assets
		ContainingCage->ScanAndRegisterContainedAssets();

		// Return the affected cage - caller will mark it dirty
		OutAffectedCage = ContainingCage;
		bAffectedAnything = true;
	}

	// Check if this actor was in a tracked palette
	TWeakObjectPtr<APCGExValencyAssetPalette>* PalettePtr = TrackedActorPaletteMap.Find(DeletedActor);
	if (PalettePtr && PalettePtr->IsValid())
	{
		APCGExValencyAssetPalette* ContainingPalette = PalettePtr->Get();

		// Refresh the palette's scanned assets
		ContainingPalette->ScanAndRegisterContainedAssets();
		bAffectedAnything = true;
		// Note: Palette dirty marking is handled by dirty state manager's mirror cascade
	}

	// Clean up tracking state
	TrackedActors.RemoveAll([DeletedActor](const TWeakObjectPtr<AActor>& ActorPtr)
	{
		return ActorPtr.Get() == DeletedActor || !ActorPtr.IsValid();
	});
	TrackedActorCageMap.Remove(DeletedActor);
	TrackedActorPaletteMap.Remove(DeletedActor);
	TrackedActorTransforms.Remove(DeletedActor);

	return bAffectedAnything;
}

bool FPCGExValencyAssetTracker::Update(TSet<APCGExValencyCage*>& OutAffectedCages, TSet<APCGExValencyAssetPalette*>& OutAffectedPalettes)
{
	OutAffectedCages.Empty();
	OutAffectedPalettes.Empty();

	if (TrackedActors.Num() == 0)
	{
		return false;
	}

	// Collect containers that can receive assets
	TArray<APCGExValencyCage*> TrackingCages;
	TArray<APCGExValencyAssetPalette*> TrackingPalettes;
	CollectTrackingCages(TrackingCages);
	CollectTrackingPalettes(TrackingPalettes);

	if (TrackingCages.Num() == 0 && TrackingPalettes.Num() == 0)
	{
		return false;
	}

	// Check each tracked actor
	for (int32 i = TrackedActors.Num() - 1; i >= 0; --i)
	{
		AActor* Actor = TrackedActors[i].Get();
		if (!Actor)
		{
			TrackedActors.RemoveAt(i);
			continue;
		}

		const FTransform CurrentTransform = Actor->GetActorTransform();
		const FVector CurrentPos = CurrentTransform.GetLocation();

		// Check if this is a new actor or if transform changed
		FTransform* LastTransform = TrackedActorTransforms.Find(Actor);
		const bool bIsNewActor = (LastTransform == nullptr);

		// Check position change (for containment detection)
		const bool bHasMoved = LastTransform &&
			FVector::DistSquared(LastTransform->GetLocation(), CurrentPos) > KINDA_SMALL_NUMBER;

		// Check full transform change (position, rotation, scale) for local transform preservation
		// Use a small tolerance for floating point comparison
		const bool bTransformChanged = LastTransform && !LastTransform->Equals(CurrentTransform, 0.01f);

		// Update tracked transform
		TrackedActorTransforms.Add(Actor, CurrentTransform);

		// ========== Cage Containment Tracking ==========
		{
			APCGExValencyCage* NewContainingCage = FindContainingCage(Actor);

			TWeakObjectPtr<APCGExValencyCage>* OldCagePtr = TrackedActorCageMap.Find(Actor);
			APCGExValencyCage* OldContainingCage = OldCagePtr ? OldCagePtr->Get() : nullptr;

			const bool bContainmentChanged = (NewContainingCage != OldContainingCage);

			// Refresh if any part of transform changed within a cage that tracks transforms.
			// This covers both explicit local-transform preservation and connector strategies
			// that depend on asset-to-cage relative transforms (e.g., AssetRelative).
			// ScanAndRegisterContainedAssets gates the cascade with HaveScannedAssetsChanged,
			// so this won't cause spurious rebuilds when scan results are unchanged.
			const bool bTransformChangedWithinCage = bTransformChanged &&
				NewContainingCage &&
				NewContainingCage == OldContainingCage &&
				(NewContainingCage->bPreserveLocalTransforms || NewContainingCage->IsConnectorTransformSensitive());

			// For newly selected actors, just record the mapping but don't trigger rebuild
			// (they're already in the cage, selecting them shouldn't cause regeneration)
			if (bIsNewActor && NewContainingCage != nullptr)
			{
				TrackedActorCageMap.Add(Actor, NewContainingCage);
			}
			else if (bContainmentChanged || bTransformChangedWithinCage)
			{
				if (OldContainingCage)
				{
					OutAffectedCages.Add(OldContainingCage);
				}
				if (NewContainingCage)
				{
					OutAffectedCages.Add(NewContainingCage);
				}

				if (NewContainingCage)
				{
					TrackedActorCageMap.Add(Actor, NewContainingCage);
				}
				else
				{
					TrackedActorCageMap.Remove(Actor);
				}
			}
		}

		// ========== Palette Containment Tracking ==========
		{
			APCGExValencyAssetPalette* NewContainingPalette = FindContainingPalette(Actor);

			TWeakObjectPtr<APCGExValencyAssetPalette>* OldPalettePtr = TrackedActorPaletteMap.Find(Actor);
			APCGExValencyAssetPalette* OldContainingPalette = OldPalettePtr ? OldPalettePtr->Get() : nullptr;

			const bool bContainmentChanged = (NewContainingPalette != OldContainingPalette);

			// Refresh if any part of transform changed within a palette that tracks transforms.
			// This covers both explicit local-transform preservation and connector strategies
			// that depend on asset-to-cage relative transforms (e.g., AssetRelative).
			// ScanAndRegisterContainedAssets gates the cascade with HaveScannedAssetsChanged,
			// so this won't cause spurious rebuilds when scan results are unchanged.
			const bool bTransformChangedWithinPalette = bTransformChanged &&
				NewContainingPalette &&
				NewContainingPalette == OldContainingPalette &&
				(NewContainingPalette->bPreserveLocalTransforms || NewContainingPalette->IsConnectorTransformSensitive());

			// For newly selected actors, just record the mapping but don't trigger rebuild
			// (they're already in the palette, selecting them shouldn't cause regeneration)
			if (bIsNewActor && NewContainingPalette != nullptr)
			{
				TrackedActorPaletteMap.Add(Actor, NewContainingPalette);
			}
			else if (bContainmentChanged || bTransformChangedWithinPalette)
			{
				if (OldContainingPalette)
				{
					OutAffectedPalettes.Add(OldContainingPalette);
				}
				if (NewContainingPalette)
				{
					OutAffectedPalettes.Add(NewContainingPalette);
				}

				if (NewContainingPalette)
				{
					TrackedActorPaletteMap.Add(Actor, NewContainingPalette);
				}
				else
				{
					TrackedActorPaletteMap.Remove(Actor);
				}
			}
		}
	}

	// Refresh affected cages
	for (APCGExValencyCage* Cage : OutAffectedCages)
	{
		if (Cage)
		{
			Cage->ScanAndRegisterContainedAssets();
		}
	}

	// Refresh affected palettes
	for (APCGExValencyAssetPalette* Palette : OutAffectedPalettes)
	{
		if (Palette)
		{
			Palette->ScanAndRegisterContainedAssets();
		}
	}

	return OutAffectedCages.Num() > 0 || OutAffectedPalettes.Num() > 0;
}

bool FPCGExValencyAssetTracker::ShouldIgnoreActor(AActor* Actor) const
{
	if (!Actor || !CachedVolumes)
	{
		return true;
	}

	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : *CachedVolumes)
	{
		if (const AValencyContextVolume* Volume = VolumePtr.Get())
		{
			if (Volume->bAutoTrackAssetPlacement && Volume->ShouldIgnoreActor(Actor))
			{
				return true;
			}
		}
	}

	return false;
}

APCGExValencyCage* FPCGExValencyAssetTracker::FindContainingCage(AActor* Actor) const
{
	if (!Actor || !CachedCages)
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : *CachedCages)
	{
		if (APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			if (!Cage->IsNullCage() && Cage->IsActorInside(Actor))
			{
				return Cage;
			}
		}
	}

	return nullptr;
}

void FPCGExValencyAssetTracker::CollectTrackingCages(TArray<APCGExValencyCage*>& OutCages) const
{
	OutCages.Empty();

	if (!CachedCages)
	{
		return;
	}

	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : *CachedCages)
	{
		if (APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			if (!Cage->IsNullCage())
			{
				OutCages.Add(Cage);
			}
		}
	}
}

void FPCGExValencyAssetTracker::FindCagesThatMirror(APCGExValencyCage* SourceCage, TArray<APCGExValencyCage*>& OutMirroringCages) const
{
	OutMirroringCages.Empty();

	if (!SourceCage || !CachedCages)
	{
		return;
	}

	// Find all cages that have SourceCage in their MirrorSources
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : *CachedCages)
	{
		if (APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			if (Cage == SourceCage)
			{
				continue; // Skip the source cage itself
			}

			// Check if this cage mirrors the source cage
			for (const FPCGExMirrorSource& MirrorEntry : Cage->MirrorSources)
			{
				if (MirrorEntry.Source == SourceCage)
				{
					OutMirroringCages.Add(Cage);
					break;
				}
			}
		}
	}
}

APCGExValencyAssetPalette* FPCGExValencyAssetTracker::FindContainingPalette(AActor* Actor) const
{
	if (!Actor || !CachedPalettes)
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr : *CachedPalettes)
	{
		if (APCGExValencyAssetPalette* Palette = PalettePtr.Get())
		{
			if (Palette->IsActorInside(Actor))
			{
				return Palette;
			}
		}
	}

	return nullptr;
}

void FPCGExValencyAssetTracker::CollectTrackingPalettes(TArray<APCGExValencyAssetPalette*>& OutPalettes) const
{
	OutPalettes.Empty();

	if (!CachedPalettes)
	{
		return;
	}

	for (const TWeakObjectPtr<APCGExValencyAssetPalette>& PalettePtr : *CachedPalettes)
	{
		if (APCGExValencyAssetPalette* Palette = PalettePtr.Get())
		{
			OutPalettes.Add(Palette);
		}
	}
}
