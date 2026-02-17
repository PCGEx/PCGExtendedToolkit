// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Cages/PCGExValencyAssetPalette.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyAssetUtils.h"
#include "Volumes/ValencyContextVolume.h"

#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "PCGDataAsset.h"
#include "PCGExValencyMacros.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

#if WITH_EDITOR
#include "Editor.h"
#include "PCGExValencyEditorSettings.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#endif

namespace PCGExValencyFolders
{
	const FName PalettesFolder = FName(TEXT("Valency/Palettes"));
}

APCGExValencyAssetPalette::APCGExValencyAssetPalette()
{
	PrimaryActorTick.bCanEverTick = false;

	// Configure as editor-only
	bNetLoadOnClient = false;
	bReplicates = false;

	// Create root component
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Create box component as default subobject (persists with the actor)
	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxBounds"));
	BoxComponent->SetupAttachment(Root);
	BoxComponent->SetBoxExtent(DetectionExtent);
	BoxComponent->SetLineThickness(2.0f);
	BoxComponent->ShapeColor = PaletteColor.ToFColor(true);
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BoxComponent->SetHiddenInGame(true);
	BoxComponent->bVisibleInReflectionCaptures = false;

	// Override base default: palettes default to preserving all transform flags
	LocalTransformFlags = static_cast<uint8>(EPCGExLocalTransformFlags::All);
}

#pragma region APCGExValencyAssetPalette

void APCGExValencyAssetPalette::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// Update shape visibility and size to match current settings
	UpdateShapeComponent();
#endif
}

void APCGExValencyAssetPalette::PostActorCreated()
{
	Super::PostActorCreated();

	// Auto-organize into Valency/Palettes folder
	SetFolderPath(PCGExValencyFolders::PalettesFolder);

	// Ensure shape visibility matches current settings
	UpdateShapeComponent();

	// Newly created palettes don't need deferred initialization
	// (they start empty and user adds content interactively)
	bNeedsInitialScan = false;
}

void APCGExValencyAssetPalette::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Update shape visibility to match current settings
	UpdateShapeComponent();

	// Note: We don't scan here anymore - palettes use lazy initialization via EnsureInitialized()
	// This is called automatically when a cage accesses the palette's content
}

void APCGExValencyAssetPalette::OnRebuildMetaTagTriggered()
{
	RequestRebuildForMirroringCages();
}

void APCGExValencyAssetPalette::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	// Update box when detection settings or color change
	if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetPalette, DetectionExtent) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetPalette, PaletteColor))
	{
		UpdateShapeComponent();

		// Re-scan if extent changed and auto-registration is enabled
		if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetPalette, DetectionExtent) && bAutoRegisterContainedAssets)
		{
			ScanAndRegisterContainedAssets();
		}
	}

	// Re-scan when auto-registration is toggled on
	if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetContainerBase, bAutoRegisterContainedAssets))
	{
		if (bAutoRegisterContainedAssets)
		{
			ScanAndRegisterContainedAssets();
		}
	}

	// Trigger rebuild when manual assets change (with debouncing)
	if (MemberName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetContainerBase, ManualAssetEntries))
	{
		if (UPCGExValencyEditorSettings::ShouldAllowRebuild(PropertyChangedEvent.ChangeType))
		{
			OnAssetRegistrationChanged();
		}
	}

	// Trigger rebuild when module settings change (with debouncing)
	if (MemberName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetContainerBase, ModuleSettings))
	{
		if (UPCGExValencyEditorSettings::ShouldAllowRebuild(PropertyChangedEvent.ChangeType))
		{
			RequestRebuildForMirroringCages();
		}
	}

	// Re-scan when transform preservation settings change
	// Rebuild is handled by OnRebuildMetaTagTriggered via PCGEX_ValencyRebuild meta tag
	if (PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetContainerBase, bPreserveLocalTransforms) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(APCGExValencyAssetContainerBase, LocalTransformFlags))
	{
		if (bAutoRegisterContainedAssets)
		{
			ScanAndRegisterContainedAssets();
		}
	}
}

void APCGExValencyAssetPalette::BeginDestroy()
{
	// Trigger rebuild for mirroring cages before destruction
	// (only if world is valid and not being torn down)
#if WITH_EDITOR
	if (UWorld* World = GetWorld())
	{
		if (!World->bIsTearingDown && !World->IsPlayInEditor())
		{
			TriggerAutoRebuildForMirroringCages();
		}
	}
#endif

	Super::BeginDestroy();
}

void APCGExValencyAssetPalette::PostEditMove(bool bFinished)
{
	// Capture current scanned assets before movement processing
	TArray<FPCGExValencyAssetEntry> OldScannedAssets;
	if (bFinished && bAutoRegisterContainedAssets && AValencyContextVolume::IsValencyModeActive())
	{
		OldScannedAssets = ScannedAssetEntries;
	}

	Super::PostEditMove(bFinished);

	// Re-scan after movement if auto-registration is enabled
	if (bFinished && bAutoRegisterContainedAssets)
	{
		ScanAndRegisterContainedAssets();

		// Check if assets changed and trigger rebuild for mirroring cages
		if (AValencyContextVolume::IsValencyModeActive() && HaveScannedAssetsChanged(OldScannedAssets))
		{
			RequestRebuildForMirroringCages();
		}
	}
}

FString APCGExValencyAssetPalette::GetPaletteDisplayName() const
{
	if (!PaletteName.IsEmpty())
	{
		return PaletteName;
	}

	const int32 ManualCount = ManualAssetEntries.Num();
	const int32 ScannedCount = ScannedAssetEntries.Num();
	const int32 TotalCount = ManualCount + ScannedCount;

	if (TotalCount > 0)
	{
		if (ManualCount > 0 && ScannedCount > 0)
		{
			return FString::Printf(TEXT("Palette [%d+%d assets]"), ManualCount, ScannedCount);
		}
		return FString::Printf(TEXT("Palette [%d assets]"), TotalCount);
	}

	return TEXT("Palette (Empty)");
}

void APCGExValencyAssetPalette::EnsureInitialized()
{
	if (bNeedsInitialScan && bAutoRegisterContainedAssets)
	{
		ScanAndRegisterContainedAssets();
		// Note: ScanAndRegisterContainedAssets clears bNeedsInitialScan
	}
}

TArray<FPCGExValencyAssetEntry> APCGExValencyAssetPalette::GetAllAssetEntries() const
{
	// Ensure we're initialized before returning entries
	// Cast away const for lazy initialization pattern
	const_cast<APCGExValencyAssetPalette*>(this)->EnsureInitialized();

	return Super::GetAllAssetEntries();
}

bool APCGExValencyAssetPalette::IsActorInside_Implementation(AActor* Actor) const
{
	if (!Actor || Actor == this)
	{
		return false;
	}

	return ContainsPoint(Actor->GetActorLocation());
}

bool APCGExValencyAssetPalette::ContainsPoint_Implementation(const FVector& WorldLocation) const
{
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);

	return FMath::Abs(LocalLocation.X) <= DetectionExtent.X &&
		   FMath::Abs(LocalLocation.Y) <= DetectionExtent.Y &&
		   FMath::Abs(LocalLocation.Z) <= DetectionExtent.Z;
}

void APCGExValencyAssetPalette::ScanAndRegisterContainedAssets()
{
	if (!bAutoRegisterContainedAssets)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Snapshot current state for change detection
	TArray<FPCGExValencyAssetEntry> OldScannedAssets = MoveTemp(ScannedAssetEntries);

	// Clear previous scanned entries
	ScannedAssetEntries.Empty();
	DiscoveredMaterialVariants.Empty();

	// Scan for actors
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == this)
		{
			continue;
		}

		// Skip other palettes, cages, and volumes
		if (Actor->IsA<APCGExValencyAssetPalette>() || Actor->IsA<APCGExValencyCageBase>() || Actor->IsA<AValencyContextVolume>())
		{
			continue;
		}

		if (IsActorInside(Actor))
		{
			if (UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>())
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					// Extract material overrides for this specific actor
					TArray<FPCGExValencyMaterialOverride> Overrides;
					ExtractMaterialOverrides(SMC, Overrides);

					// Create material variant if overrides exist
					const bool bHasOverrides = Overrides.Num() > 0;
					FPCGExValencyMaterialVariant Variant;
					if (bHasOverrides)
					{
						Variant.Overrides = MoveTemp(Overrides);
						Variant.DiscoveryCount = 1;
					}

					// Add to scanned entries with material variant info
					AddScannedEntry(TSoftObjectPtr<UObject>(Mesh), Actor, bHasOverrides ? &Variant : nullptr);
				}
			}
			else if (UClass* ActorClass = Actor->GetClass())
			{
				if (UBlueprint* BP = Cast<UBlueprint>(ActorClass->ClassGeneratedBy))
				{
					AddScannedEntry(TSoftObjectPtr<UObject>(BP), Actor, nullptr);
				}
			}
		}
	}

	// Also check attached children
	TArray<AActor*> ChildActors;
	GetAttachedActors(ChildActors);

	for (AActor* Child : ChildActors)
	{
		if (Child && !Child->IsA<APCGExValencyAssetPalette>() && !Child->IsA<APCGExValencyCageBase>())
		{
			if (UStaticMeshComponent* SMC = Child->FindComponentByClass<UStaticMeshComponent>())
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					// Extract material overrides for this child actor
					TArray<FPCGExValencyMaterialOverride> Overrides;
					ExtractMaterialOverrides(SMC, Overrides);

					const bool bHasOverrides = Overrides.Num() > 0;
					FPCGExValencyMaterialVariant Variant;
					if (bHasOverrides)
					{
						Variant.Overrides = MoveTemp(Overrides);
						Variant.DiscoveryCount = 1;
					}

					AddScannedEntry(TSoftObjectPtr<UObject>(Mesh), Child, bHasOverrides ? &Variant : nullptr);
				}
			}
		}
	}

	// Mark as initialized - no longer needs initial scan
	bNeedsInitialScan = false;

	// Only trigger cascade if scan results actually changed
	if (HaveScannedAssetsChanged(OldScannedAssets))
	{
		OnAssetRegistrationChanged();
	}
}

void APCGExValencyAssetPalette::UpdateShapeComponent()
{
	if (BoxComponent)
	{
		BoxComponent->SetBoxExtent(DetectionExtent);
		BoxComponent->ShapeColor = PaletteColor.ToFColor(true);
	}
}

void APCGExValencyAssetPalette::OnAssetRegistrationChanged()
{
	Modify();

	// Trigger rebuild for cages that mirror this palette via unified dirty state system
	RequestRebuildForMirroringCages();

	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
}

bool APCGExValencyAssetPalette::TriggerAutoRebuildForMirroringCages()
{
	// Use centralized reference tracker for recursive propagation
	if (FValencyReferenceTracker* Tracker = UPCGExValencyCageEditorMode::GetActiveReferenceTracker())
	{
		return Tracker->PropagateContentChange(this);
	}
	return false;
}

void APCGExValencyAssetPalette::RequestRebuildForMirroringCages()
{
	// Find all cages that mirror this palette and request rebuild on each
	TArray<APCGExValencyCage*> MirroringCages;
	FindMirroringCages(MirroringCages);

	for (APCGExValencyCage* Cage : MirroringCages)
	{
		if (Cage)
		{
			Cage->RequestRebuild(EValencyRebuildReason::ExternalCascade);
		}
	}
}

#pragma endregion
