// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Volumes/ValencyContextVolume.h"

#include "EngineUtils.h"
#include "Components/BrushComponent.h"
#include "PCGComponent.h"
#include "PCGExValencyEditorCommon.h"
#include "PCGExValencyMacros.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCageSpatialRegistry.h"
#include "Builder/PCGExValencyBondingRulesBuilder.h"
#include "Subsystems/PCGSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "EditorModeManager.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "EditorMode/PCGExValencyDirtyState.h"
#include "PCGExValencyEditorSettings.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogValencyVolume, Log, All);


AValencyContextVolume::AValencyContextVolume()
{
	// Configure as editor-only
	bNetLoadOnClient = false;

	// Set up brush component defaults
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		// Enable query-only collision for editor selection (no physics)
		BrushComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BrushComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		BrushComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		BrushComp->SetMobility(EComponentMobility::Static);
	}
}

void AValencyContextVolume::PostActorCreated()
{
	Super::PostActorCreated();

	// Auto-organize into Valency/Volumes folder
	SetFolderPath(PCGExValencyFolders::VolumesFolder);
}

void AValencyContextVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Ensure brush collision is set for editor selection
	// (Constructor settings may not persist through serialization)
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BrushComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		BrushComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	// Notify all cages in the world to check if they're now contained by us
	// This handles the initialization order problem: cages may have initialized
	// before volumes, so their ContainingVolumes list would be empty
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APCGExValencyCageBase> It(World); It; ++It)
		{
			APCGExValencyCageBase* Cage = *It;
			if (Cage)
			{
				// Refresh this cage's containing volumes list
				// This is cheap - just iterates volumes and checks containment
				Cage->RefreshContainingVolumes();
			}
		}
	}
}

void AValencyContextVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Notify cages if relevant properties changed
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AValencyContextVolume, BondingRules) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AValencyContextVolume, DefaultProbeRadius))
	{
		NotifyContainedCages();
	}

	// Check if any property in the chain has PCGEX_ValencyRebuild metadata
	bool bShouldRebuild = false;
	bool bFoundMetadata = false;

	if (const FProperty* Property = PropertyChangedEvent.Property)
	{
		if (Property->HasMetaData(TEXT("PCGEX_ValencyRebuild")))
		{
			bShouldRebuild = true;
			bFoundMetadata = true;
		}
	}

	if (!bShouldRebuild && PropertyChangedEvent.MemberProperty)
	{
		if (PropertyChangedEvent.MemberProperty->HasMetaData(TEXT("PCGEX_ValencyRebuild")))
		{
			bShouldRebuild = true;
			bFoundMetadata = true;
		}
	}

	// Debounce interactive changes (dragging sliders) to prevent spam
	if (bShouldRebuild && !UPCGExValencyEditorSettings::ShouldAllowRebuild(PropertyChangedEvent.ChangeType))
	{
		bShouldRebuild = false;
	}

	if (bShouldRebuild && bAutoRebuildOnChange && IsValencyModeActive())
	{
		// Use dirty state system for proper coalescing and deferred rebuild
		if (FValencyDirtyStateManager* Manager = APCGExValencyCageBase::GetActiveDirtyStateManager())
		{
			Manager->MarkVolumeDirty(this, EValencyDirtyFlags::ModuleSettings);
		}
		else
		{
			// Fallback to direct rebuild if manager not available
			BuildRulesFromCages();
		}
	}
}

void AValencyContextVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished)
	{
		// Volume moved - cages may need to recalculate their containing volumes
		NotifyContainedCages();
	}
}

UPCGExValencyOrbitalSet* AValencyContextVolume::GetEffectiveOrbitalSet() const
{
	return BondingRules ? BondingRules->OrbitalSet : nullptr;
}

UPCGExValencyConnectorSet* AValencyContextVolume::GetEffectiveConnectorSet() const
{
	return (BondingRules && BondingRules->ConnectorSet) ? BondingRules->ConnectorSet : nullptr;
}

bool AValencyContextVolume::ContainsPoint(const FVector& WorldLocation, float Tolerance) const
{
	// Use volume's built-in EncompassesPoint
	float OutDistanceToPoint = 0.0f;
	return EncompassesPoint(WorldLocation, Tolerance, &OutDistanceToPoint);
}

void AValencyContextVolume::CollectContainedCages(TArray<APCGExValencyCageBase*>& OutCages) const
{
	OutCages.Empty();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APCGExValencyCageBase> It(World); It; ++It)
		{
			APCGExValencyCageBase* Cage = *It;
			if (Cage && ContainsPoint(Cage->GetActorLocation()))
			{
				OutCages.Add(Cage);
			}
		}
	}
}

void AValencyContextVolume::RebuildBondingRules()
{
	BuildRulesFromCages();
}

void AValencyContextVolume::BuildRulesFromCages()
{
	if (!BondingRules)
	{
		UE_LOG(LogValencyVolume, Error, TEXT("Cannot build rules: No BondingRules asset assigned to volume."));
		return;
	}

	// Find all volumes in this level that share the same BondingRules
	TArray<AValencyContextVolume*> RelatedVolumes;
	FindRelatedVolumes(RelatedVolumes);

	// Get current level name for cross-level warning
	UWorld* World = GetWorld();
	FString CurrentLevelName;
	if (World)
	{
		CurrentLevelName = World->GetMapName();
	}

#if WITH_EDITOR
	// Check if this build is from a different level than the last one
	if (!BondingRules->LastBuildLevelPath.IsEmpty() &&
		!CurrentLevelName.IsEmpty() &&
		BondingRules->LastBuildLevelPath != CurrentLevelName)
	{
		const FText WarningMessage = FText::Format(
			NSLOCTEXT("ValencyVolume", "CrossLevelWarning",
				"This BondingRules asset was last built from a different level:\n\n"
				"Last build: {0}\n"
				"Current level: {1}\n\n"
				"Building from this level will overwrite the existing rules.\n"
				"Are you sure you want to continue?"),
			FText::FromString(BondingRules->LastBuildLevelPath),
			FText::FromString(CurrentLevelName)
		);

		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::YesNo,
			WarningMessage,
			NSLOCTEXT("ValencyVolume", "CrossLevelWarningTitle", "Cross-Level Build Warning")
		);

		if (Response != EAppReturnType::Yes)
		{
			UE_LOG(LogValencyVolume, Log, TEXT("Build cancelled by user due to cross-level warning."));
			return;
		}
	}
#endif

	UE_LOG(LogValencyVolume, Log, TEXT("Building rules from %d related volume(s)."), RelatedVolumes.Num());

	UPCGExValencyBondingRulesBuilder* Builder = NewObject<UPCGExValencyBondingRulesBuilder>(GetTransientPackage());
	FPCGExValencyBuildResult Result = Builder->BuildFromVolumes(RelatedVolumes, this);

	// Log results
	if (Result.bSuccess)
	{
		UE_LOG(LogValencyVolume, Log, TEXT("Build succeeded: %d modules from %d cages."), Result.ModuleCount, Result.CageCount);

		// Regenerate PCG actors from ALL related volumes (if enabled)
		if (UPCGExValencyEditorSettings::Get()->bAutoRegeneratePCG)
		{
			for (AValencyContextVolume* Volume : RelatedVolumes)
			{
				if (Volume)
				{
					Volume->RegeneratePCGActors();
				}
			}
		}
	}
	else
	{
		UE_LOG(LogValencyVolume, Error, TEXT("Build failed."));
	}

	for (const FText& Warning : Result.Warnings)
	{
		UE_LOG(LogValencyVolume, Warning, TEXT("%s"), *Warning.ToString());
	}

	for (const FText& Error : Result.Errors)
	{
		UE_LOG(LogValencyVolume, Error, TEXT("%s"), *Error.ToString());
	}
}

void AValencyContextVolume::NotifyContainedCages()
{
	TArray<APCGExValencyCageBase*> ContainedCages;
	CollectContainedCages(ContainedCages);

	for (APCGExValencyCageBase* Cage : ContainedCages)
	{
		if (Cage)
		{
			Cage->OnContainingVolumeChanged(this);
		}
	}
}

void AValencyContextVolume::RefreshCageRelationships()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Collect cages inside the volume
	TArray<APCGExValencyCageBase*> ContainedCages;
	CollectContainedCages(ContainedCages);

	// Also collect ALL cages that might be spatially related (within max probe radius)
	// This handles cages that were dragged in/out or are just outside the volume but connected
	TSet<APCGExValencyCageBase*> AllAffectedCages;

	// Add contained cages
	for (APCGExValencyCageBase* Cage : ContainedCages)
	{
		AllAffectedCages.Add(Cage);
	}

	// For each contained cage, find cages within probe radius (even if outside volume)
	FPCGExValencyCageSpatialRegistry& Registry = FPCGExValencyCageSpatialRegistry::Get(World);

	for (APCGExValencyCageBase* Cage : ContainedCages)
	{
		if (!Cage)
		{
			continue;
		}

		const float ProbeRadius = Cage->GetEffectiveProbeRadius();
		const float MaxRadius = FMath::Max(ProbeRadius, Registry.GetMaxProbeRadius());

		TArray<APCGExValencyCageBase*> NearbyCages;
		Registry.FindCagesNearPosition(Cage->GetActorLocation(), MaxRadius, NearbyCages, nullptr);

		for (APCGExValencyCageBase* NearbyCage : NearbyCages)
		{
			AllAffectedCages.Add(NearbyCage);
		}
	}

	UE_LOG(LogValencyVolume, Log, TEXT("Refreshing relationships for %d cages (%d in volume, %d total affected)."),
		ContainedCages.Num(), ContainedCages.Num(), AllAffectedCages.Num());

	// First pass: refresh all cages' volumes and initialize orbitals
	for (APCGExValencyCageBase* Cage : AllAffectedCages)
	{
		if (Cage)
		{
			Cage->RefreshContainingVolumes();
			Cage->InitializeOrbitalsFromSet();
		}
	}

	// Second pass: detect connections for each cage
	for (APCGExValencyCageBase* Cage : AllAffectedCages)
	{
		if (Cage)
		{
			Cage->DetectNearbyConnections();
		}
	}

	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT

	UE_LOG(LogValencyVolume, Log, TEXT("Cage relationships refreshed."));
}

void AValencyContextVolume::RegeneratePCGActors()
{
	if (PCGActorsToRegenerate.Num() == 0)
	{
		return;
	}

	// Defer actual regeneration by one frame. During rapid interactive changes (slider drags),
	// the async FlushCache from frame N races with Generate from frame N+1 — the late-arriving
	// flush wipes freshly generated data (visible as flash-then-blank).
	// By deferring, each new slider tick just resets the pending frame counter, so the actual
	// flush+generate only runs once the slider stops (quiet frame). Rules are still updated
	// every frame by BuildRulesFromCages — only the expensive PCG regeneration is deferred.
	PendingRegenerateFrame = GFrameCounter;
}

void AValencyContextVolume::ExecutePendingRegenerate()
{
	PendingRegenerateFrame = 0;
	ExecuteRegenerate();
}

void AValencyContextVolume::ExecuteRegenerate()
{
	if (PCGActorsToRegenerate.Num() == 0)
	{
		return;
	}

	int32 RegeneratedCount = 0;

	// Flush PCG cache before regeneration (safe — no overlapping cycle)
	if (UPCGExValencyEditorSettings::Get()->bFlushPCGCacheOnRegenerate)
	{
		if (UPCGSubsystem* Subsystem = UPCGSubsystem::GetActiveEditorInstance())
		{
			Subsystem->FlushCache();
		}
	}

	for (const TObjectPtr<AActor>& ActorPtr : PCGActorsToRegenerate)
	{
		AActor* Actor = ActorPtr.Get();
		if (!Actor)
		{
			continue;
		}

		// Find all PCG components on this actor
		TArray<UPCGComponent*> PCGComponents;
		Actor->GetComponents<UPCGComponent>(PCGComponents);

		for (UPCGComponent* PCGComponent : PCGComponents)
		{
			if (!PCGComponent)
			{
				continue;
			}

			// Cleanup (removes generated components) and regenerate
			PCGComponent->Cleanup(true);
			PCGComponent->Generate(true);
			RegeneratedCount++;
		}
	}

	if (RegeneratedCount > 0)
	{
		UE_LOG(LogValencyVolume, Log, TEXT("Regenerated %d PCG component(s) on %d actor(s)."), RegeneratedCount, PCGActorsToRegenerate.Num());
	}
}

bool AValencyContextVolume::ShouldIgnoreActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return true;
	}

	// Check if actor has any of the ignored tags
	for (const FName& IgnoredTag : IgnoredActorTags)
	{
		if (Actor->ActorHasTag(IgnoredTag))
		{
			return true;
		}
	}

	// Check if actor is in the explicit ignore list
	for (const TObjectPtr<AActor>& IgnoredActor : IgnoredActors)
	{
		if (IgnoredActor.Get() == Actor)
		{
			return true;
		}
	}

	// Check if actor is spawned by/attached to PCG actors we regenerate
	if (bAutoIgnorePCGSpawnedActors && PCGActorsToRegenerate.Num() > 0)
	{
		// Walk up the attachment chain to see if any parent is a PCG actor to regenerate
		const AActor* Current = Actor;
		while (Current)
		{
			// Check if this actor IS one of the PCG actors
			for (const TObjectPtr<AActor>& PCGActor : PCGActorsToRegenerate)
			{
				if (PCGActor.Get() == Current)
				{
					return true;
				}
			}

			// Move up to parent
			Current = Current->GetAttachParentActor();
		}

		// Also check owner chain (for actors spawned by PCG but not attached)
		Current = Actor->GetOwner();
		while (Current)
		{
			for (const TObjectPtr<AActor>& PCGActor : PCGActorsToRegenerate)
			{
				if (PCGActor.Get() == Current)
				{
					return true;
				}
			}
			Current = Current->GetOwner();
		}
	}

	return false;
}

void AValencyContextVolume::FindRelatedVolumes(TArray<AValencyContextVolume*>& OutVolumes) const
{
	OutVolumes.Empty();

	if (!BondingRules)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Find all volumes that reference the same BondingRules asset
	for (TActorIterator<AValencyContextVolume> It(World); It; ++It)
	{
		AValencyContextVolume* Volume = *It;
		if (Volume && Volume->BondingRules == BondingRules)
		{
			OutVolumes.Add(Volume);
		}
	}
}

bool AValencyContextVolume::IsValencyModeActive()
{
#if WITH_EDITOR
	if (GLevelEditorModeTools().IsModeActive(UPCGExValencyCageEditorMode::ModeID))
	{
		return true;
	}
#endif
	return false;
}
