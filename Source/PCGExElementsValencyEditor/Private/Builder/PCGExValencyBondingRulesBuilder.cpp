// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Builder/PCGExValencyBondingRulesBuilder.h"

#include "Core/PCGExBondingRulesAssembler.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "PCGExValencyEditorCommon.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "PCGExPropertyCollectionComponent.h"
#include "Volumes/ValencyContextVolume.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Core/PCGExValencyLog.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "PCGExValencyBuilder"

FPCGExValencyBuildResult UPCGExValencyBondingRulesBuilder::BuildFromVolume(AValencyContextVolume* Volume)
{
	if (!Volume)
	{
		FPCGExValencyBuildResult Result;
		Result.Errors.Add(LOCTEXT("NoVolume", "No volume provided."));
		return Result;
	}

	// Delegate to multi-volume version with single volume
	return BuildFromVolumes({Volume}, Volume);
}

FPCGExValencyBuildResult UPCGExValencyBondingRulesBuilder::BuildFromVolumes(const TArray<AValencyContextVolume*>& Volumes, AValencyContextVolume* TriggeringVolume)
{
	FPCGExValencyBuildResult Result;

	if (Volumes.Num() == 0)
	{
		Result.Errors.Add(LOCTEXT("NoVolumes", "No volumes provided."));
		return Result;
	}

	// Use first volume to get shared resources, or triggering volume if provided
	AValencyContextVolume* PrimaryVolume = TriggeringVolume ? TriggeringVolume : Volumes[0];

	UPCGExValencyBondingRules* TargetRules = PrimaryVolume->GetBondingRules();
	if (!TargetRules)
	{
		Result.Errors.Add(LOCTEXT("NoBondingRules", "Primary volume has no BondingRules asset assigned."));
		return Result;
	}

	UPCGExValencyOrbitalSet* OrbitalSet = PrimaryVolume->GetEffectiveOrbitalSet();
	if (!OrbitalSet)
	{
		Result.Errors.Add(LOCTEXT("NoOrbitalSet", "Primary volume has no OrbitalSet (check BondingRules or override)."));
		return Result;
	}

	// Verify all volumes reference the same BondingRules
	for (AValencyContextVolume* Volume : Volumes)
	{
		if (Volume && Volume->GetBondingRules() != TargetRules)
		{
			Result.Warnings.Add(FText::Format(
				LOCTEXT("MismatchedRules", "Volume '{0}' references different BondingRules - skipping."),
				FText::FromString(Volume->GetName())
			));
		}
	}

	// Collect cages from ALL volumes that share the same BondingRules
	TArray<APCGExValencyCage*> AllRegularCages;
	for (AValencyContextVolume* Volume : Volumes)
	{
		if (!Volume || Volume->GetBondingRules() != TargetRules)
		{
			continue;
		}

		// Ensure cage relationships are up-to-date before building
		Volume->RefreshCageRelationships();

		// Collect cages from this volume
		TArray<APCGExValencyCageBase*> VolumeCages;
		Volume->CollectContainedCages(VolumeCages);

		// Filter to regular cages (not null cages)
		for (APCGExValencyCageBase* CageBase : VolumeCages)
		{
			if (APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CageBase))
			{
				AllRegularCages.AddUnique(Cage);
			}
		}
	}

	// Build from all collected cages
	Result = BuildFromCages(AllRegularCages, TargetRules, OrbitalSet);

	// Compile patterns if module build succeeded
	// NOTE: We compile patterns even if ModuleCount == 0, because pattern topology
	// (adjacency, boundary masks) is still valid and useful. The pattern entries
	// just won't have ModuleIndices resolved until modules exist.
	if (Result.bSuccess)
	{
		// Rebuild ModuleKeyToIndex from compiled modules for pattern compilation
		TMap<FString, int32> ModuleKeyToIndex;

		for (int32 ModuleIndex = 0; ModuleIndex < TargetRules->Modules.Num(); ++ModuleIndex)
		{
			const FPCGExValencyModuleDefinition& Module = TargetRules->Modules[ModuleIndex];
			const int64 OrbitalMask = Module.LayerConfig.OrbitalMask;

			// LocalTransform is NOT part of module identity - always pass nullptr
			const FPCGExValencyMaterialVariant* MaterialVariantPtr = Module.bHasMaterialVariant ? &Module.MaterialVariant : nullptr;

			const FString ModuleKey = FPCGExValencyCageData::MakeModuleKey(
				Module.Asset.ToSoftObjectPath(), OrbitalMask, nullptr, MaterialVariantPtr);

			ModuleKeyToIndex.Add(ModuleKey, ModuleIndex);
		}

		// Compile patterns from all volumes
		CompilePatterns(Volumes, ModuleKeyToIndex, TargetRules, OrbitalSet, Result);

		// CRITICAL: Always sync patterns to CompiledData after CompilePatterns().
		// This ensures the runtime data has the freshly compiled patterns.
		// Previously this was gated on ModuleCount > 0, causing stale pattern data
		// when patterns were modified but no modules changed.
		if (TargetRules->IsCompiled())
		{
			TargetRules->CompiledData.CompiledPatterns = TargetRules->Patterns;
		}
	}

	// Update build metadata on success
	if (Result.bSuccess && PrimaryVolume)
	{
		if (UWorld* World = PrimaryVolume->GetWorld())
		{
			TargetRules->LastBuildLevelPath = World->GetMapName();
		}
		TargetRules->LastBuildVolumeName = PrimaryVolume->GetName();
		TargetRules->LastBuildTime = FDateTime::Now();
	}

	return Result;
}

FPCGExValencyBuildResult UPCGExValencyBondingRulesBuilder::BuildFromCages(
	const TArray<APCGExValencyCage*>& Cages,
	UPCGExValencyBondingRules* TargetRules,
	UPCGExValencyOrbitalSet* OrbitalSet)
{
	FPCGExValencyBuildResult Result;

	if (!TargetRules)
	{
		Result.Errors.Add(LOCTEXT("NoTargetRules", "No target BondingRules asset provided."));
		return Result;
	}

	if (!OrbitalSet)
	{
		Result.Errors.Add(LOCTEXT("NoOrbitalSetBuild", "No OrbitalSet provided."));
		return Result;
	}

	if (Cages.Num() == 0)
	{
		Result.Warnings.Add(LOCTEXT("NoCages", "No cages to process."));
		Result.bSuccess = true;
		return Result;
	}

	// Assign orbital set to the bonding rules
	TargetRules->OrbitalSet = OrbitalSet;

	// Phase 1: Cage-specific collection
	TArray<FPCGExValencyCageData> CageData;
	CollectCageData(Cages, OrbitalSet, CageData, Result);

	if (CageData.Num() == 0)
	{
		Result.Warnings.Add(LOCTEXT("NoValidCages", "No cages with registered assets found."));

		// Even with no valid cages, we need to compile and mark dirty
		// This ensures the PCG graph sees the cleared modules
		if (bClearExistingModules)
		{
			TargetRules->Modules.Empty();
		}

		if (!TargetRules->Compile())
		{
			Result.Errors.Add(LOCTEXT("CompileFailedEmpty", "Failed to compile empty BondingRules."));
			return Result;
		}

		TargetRules->Modify();
		TargetRules->RebuildGeneratedCollections();
		(void)TargetRules->MarkPackageDirty();

		Result.bSuccess = true;
		Result.CageCount = 0;
		Result.ModuleCount = 0;
		return Result;
	}

	// Phase 1.5: Discover material variants from mesh components
	DiscoverMaterialVariants(CageData, TargetRules);

	// Phase 2+3: Delegate module building + neighbor relationships to Assembler
	FPCGExBondingRulesAssembler Assembler;
	PopulateAssembler(CageData, OrbitalSet, Assembler);

	// Phase 4: Validate + Apply
	FPCGExAssemblerResult AssemblerResult = Assembler.Apply(TargetRules, bClearExistingModules);
	Result.Warnings.Append(MoveTemp(AssemblerResult.Warnings));
	Result.Errors.Append(MoveTemp(AssemblerResult.Errors));

	if (!AssemblerResult.bSuccess)
	{
		return Result;
	}

	TargetRules->Modify();

	// Mark asset as modified
	(void)TargetRules->MarkPackageDirty();

	Result.bSuccess = Result.Errors.Num() == 0;
	Result.CageCount = CageData.Num();
	Result.ModuleCount = TargetRules->Modules.Num();

	return Result;
}

void UPCGExValencyBondingRulesBuilder::CollectCageData(
	const TArray<APCGExValencyCage*>& Cages,
	const UPCGExValencyOrbitalSet* OrbitalSet,
	TArray<FPCGExValencyCageData>& OutCageData,
	FPCGExValencyBuildResult& OutResult)
{
	OutCageData.Empty();
	OutCageData.Reserve(Cages.Num());

	VALENCY_LOG_SECTION(Building, "COLLECTING CAGE DATA");
	PCGEX_VALENCY_INFO(Building, "Processing %d cages", Cages.Num());

	for (APCGExValencyCage* Cage : Cages)
	{
		if (!Cage)
		{
			continue;
		}

		// Skip cages disabled for compilation
		if (!Cage->bEnabledForCompilation)
		{
			PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': Disabled for compilation - skipping.", *Cage->GetCageDisplayName());
			continue;
		}

		// Trigger asset scan for cages with auto-registration enabled
		if (Cage->bAutoRegisterContainedAssets)
		{
			Cage->ScanAndRegisterContainedAssets();
		}

		// Get effective asset entries (resolving mirrors)
		TArray<FPCGExValencyAssetEntry> AssetEntries = GetEffectiveAssetEntries(Cage);
		if (AssetEntries.Num() == 0)
		{
			if (Cage->bIsTemplate)
			{
				PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': Template cage (no assets expected) - skipping module creation.",
					*Cage->GetCageDisplayName());
			}
			else
			{
				PCGEX_VALENCY_INFO(Building, "  Cage '%s': NO ASSETS after mirror resolution (own=%d, mirrors=%d) - skipping. Cages connected to this one will have missing neighbors.",
					*Cage->GetCageDisplayName(), Cage->GetAllAssetEntries().Num(), Cage->MirrorSources.Num());
			}
			// Skip cages with no assets (template or otherwise — no module to create)
			continue;
		}

		FPCGExValencyCageData Data;
		Data.Cage = Cage;
		Data.AssetEntries = MoveTemp(AssetEntries);
		Data.Settings = Cage->ModuleSettings;
		Data.PlacementPolicy = Cage->PlacementPolicy;
		Data.ModuleName = Cage->ModuleName.IsNone() ? FName(*Cage->GetCageDisplayName()) : Cage->ModuleName;
		Data.bPreserveLocalTransforms = Cage->bPreserveLocalTransforms;
		Data.ConnectorTransformStrategy = Cage->ConnectorTransformStrategy;

		// Collect properties from cage and its mirror sources (palettes act as data prefabs)
		Data.Properties = GetEffectiveProperties(Cage);
		PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': %d properties collected", *Cage->GetCageDisplayName(), Data.Properties.Num());

		// Collect actor tags from cage and its mirror sources
		Data.Tags = GetEffectiveTags(Cage);
		PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': %d tags collected", *Cage->GetCageDisplayName(), Data.Tags.Num());

		// ========== Connector Collection ==========
		// Phase 1: Auto-extract connectors from mesh assets (if enabled)
		// Phase 2: Collect connector components (can override auto-extracted)

		Data.bReadConnectorsFromAssets = Cage->bReadConnectorsFromAssets;

		// Build a map of mesh sockets from effective assets for both auto-extraction and transform matching
		TMap<FName, FTransform> MeshSocketTransforms; // SocketName -> Transform
		UPCGExValencyConnectorSet* EffectiveConnectorSet = Cage->GetEffectiveConnectorSet();

		// Collect mesh sockets from all effective assets
		for (const FPCGExValencyAssetEntry& AssetEntry : Data.AssetEntries)
		{
			if (AssetEntry.AssetType != EPCGExValencyAssetType::Mesh)
			{
				continue;
			}

			if (UStaticMesh* Mesh = Cast<UStaticMesh>(AssetEntry.Asset.LoadSynchronous()))
			{
				for (const UStaticMeshSocket* MeshSocket : Mesh->Sockets)
				{
					if (!MeshSocket)
					{
						continue;
					}

					// Store transform for later use (component transform matching)
					const FTransform SocketTransform(
						MeshSocket->RelativeRotation,
						MeshSocket->RelativeLocation,
						MeshSocket->RelativeScale
					);
					MeshSocketTransforms.Add(MeshSocket->SocketName, SocketTransform);

					// Auto-extract if enabled and connector matches rules
					if (Data.bReadConnectorsFromAssets && EffectiveConnectorSet)
					{
						const FName MatchedType = EffectiveConnectorSet->FindMatchingConnectorType(
							MeshSocket->SocketName,
							MeshSocket->Tag
						);

						if (!MatchedType.IsNone())
						{
							// Check if we already have this connector (avoid duplicates)
							bool bAlreadyExists = false;
							for (const FPCGExValencyModuleConnector& Existing : Data.Connectors)
							{
								if (Existing.Identifier == MeshSocket->SocketName)
								{
									bAlreadyExists = true;
									break;
								}
							}

							if (!bAlreadyExists)
							{
								FPCGExValencyModuleConnector AutoConnector;
								AutoConnector.Identifier = MeshSocket->SocketName;
								AutoConnector.ConnectorType = MatchedType;
								AutoConnector.LocalOffset = SocketTransform;
								AutoConnector.bOverrideOffset = true;
								AutoConnector.Polarity = EPCGExConnectorPolarity::Universal; // Auto-extracted default to universal
								AutoConnector.OrbitalIndex = -1;

								Data.Connectors.Add(AutoConnector);
								PCGEX_VALENCY_VERBOSE(Building, "    Auto-extracted connector '%s' (type=%s) from mesh",
									*MeshSocket->SocketName.ToString(), *MatchedType.ToString());
							}
						}
					}
				}
			}
		}

		const int32 AutoExtractedCount = Data.Connectors.Num();

		// Phase 2: Inherit connectors from mirror sources (medium priority)
		for (const FPCGExMirrorSource& MirrorEntry : Cage->MirrorSources)
		{
			if (!MirrorEntry.IsValid() || !MirrorEntry.ShouldMirror(EPCGExMirrorContent::Connectors))
			{
				continue;
			}

			AActor* SourceActor = MirrorEntry.Source;

			// Skip disabled source cages
			if (const APCGExValencyCageBase* SourceCageBase = Cast<APCGExValencyCageBase>(SourceActor))
			{
				if (!SourceCageBase->bEnabledForCompilation) continue;
			}

			// Collect inheritable connectors from source
			TArray<UPCGExValencyCageConnectorComponent*> SourceConnectors;
			if (APCGExValencyCageBase* SourceCage = Cast<APCGExValencyCageBase>(SourceActor))
			{
				SourceCage->GetConnectorComponents(SourceConnectors);
			}

			for (const UPCGExValencyCageConnectorComponent* SrcConn : SourceConnectors)
			{
				if (!SrcConn || !SrcConn->bEnabled || !SrcConn->bInheritable) continue;

				// Conflict resolution: existing (auto-extracted) takes precedence
				bool bAlreadyExists = false;
				for (const FPCGExValencyModuleConnector& Existing : Data.Connectors)
				{
					if (Existing.Identifier == SrcConn->Identifier)
					{
						bAlreadyExists = true;
						break;
					}
				}

				if (!bAlreadyExists)
				{
					FPCGExValencyModuleConnector InheritedConn;
					InheritedConn.Identifier = SrcConn->Identifier;
					InheritedConn.ConnectorType = SrcConn->ConnectorType;
					InheritedConn.LocalOffset = SrcConn->GetConnectorLocalTransform();
					InheritedConn.bOverrideOffset = true;
					InheritedConn.Polarity = SrcConn->Polarity;
					InheritedConn.Priority = SrcConn->Priority;
					InheritedConn.SpawnCapacity = SrcConn->SpawnCapacity;
					InheritedConn.ConstraintOverrides = SrcConn->ConstraintOverrides;
					InheritedConn.Priority = SrcConn->Priority;
					InheritedConn.SpawnCapacity = SrcConn->SpawnCapacity;
					InheritedConn.OverrideMode = SrcConn->OverrideMode;
					InheritedConn.bManualOrbitalOverride = SrcConn->bManualOrbitalOverride;
					InheritedConn.ManualOrbitalIndex = SrcConn->ManualOrbitalIndex;
					InheritedConn.OrbitalIndex = -1;

					Data.Connectors.Add(InheritedConn);
					PCGEX_VALENCY_VERBOSE(Building, "    Inherited connector '%s' from mirror source '%s'",
						*SrcConn->Identifier.ToString(), *SourceActor->GetName());
				}
			}
		}

		const int32 InheritedCount = Data.Connectors.Num() - AutoExtractedCount;

		// Phase 3: Collect local connector components (highest priority — can override both)
		TArray<UPCGExValencyCageConnectorComponent*> ConnectorComponents;
		Cage->GetConnectorComponents(ConnectorComponents);

		for (const UPCGExValencyCageConnectorComponent* ConnectorComp : ConnectorComponents)
		{
			if (!ConnectorComp || !ConnectorComp->bEnabled)
			{
				continue;
			}

			// Determine the transform to use
			FTransform SocketTransform = ConnectorComp->GetConnectorLocalTransform();

			// Handle bMatchMeshSocketTransform - inherit transform from matching mesh socket
			if (ConnectorComp->bMatchMeshSocketTransform)
			{
				// First try explicit MeshSocketName, then fall back to Identifier
				const FName NameToMatch = ConnectorComp->MeshSocketName.IsNone()
					? ConnectorComp->Identifier
					: ConnectorComp->MeshSocketName;

				if (const FTransform* MeshTransform = MeshSocketTransforms.Find(NameToMatch))
				{
					SocketTransform = *MeshTransform;
					PCGEX_VALENCY_VERBOSE(Building, "    Connector '%s' matched mesh socket transform from '%s'",
						*ConnectorComp->Identifier.ToString(), *NameToMatch.ToString());
				}
			}

			// Check if this component overrides an existing connector (auto-extracted or inherited)
			int32 ExistingIndex = INDEX_NONE;
			for (int32 i = 0; i < Data.Connectors.Num(); ++i)
			{
				if (Data.Connectors[i].Identifier == ConnectorComp->Identifier)
				{
					ExistingIndex = i;
					break;
				}
			}

			if (ExistingIndex != INDEX_NONE)
			{
				// Connector with this name already exists (from auto-extraction or inheritance)
				if (ConnectorComp->bOverrideAutoExtracted)
				{
					// Replace the existing connector with local component data
					FPCGExValencyModuleConnector& ExistingConnector = Data.Connectors[ExistingIndex];
					ExistingConnector.ConnectorType = ConnectorComp->ConnectorType;
					ExistingConnector.LocalOffset = SocketTransform;
					ExistingConnector.bOverrideOffset = true;
					ExistingConnector.Polarity = ConnectorComp->Polarity;
					ExistingConnector.ConstraintOverrides = ConnectorComp->ConstraintOverrides;
					ExistingConnector.OverrideMode = ConnectorComp->OverrideMode;
					ExistingConnector.bManualOrbitalOverride = ConnectorComp->bManualOrbitalOverride;
					ExistingConnector.ManualOrbitalIndex = ConnectorComp->ManualOrbitalIndex;
					PCGEX_VALENCY_VERBOSE(Building, "    Connector component '%s' overrides existing",
						*ConnectorComp->Identifier.ToString());
				}
				else
				{
					PCGEX_VALENCY_VERBOSE(Building, "    Connector component '%s' skipped (existing takes precedence)",
						*ConnectorComp->Identifier.ToString());
				}
			}
			else
			{
				// New connector from component
				FPCGExValencyModuleConnector ModuleConnector;
				ModuleConnector.Identifier = ConnectorComp->Identifier;
				ModuleConnector.ConnectorType = ConnectorComp->ConnectorType;
				ModuleConnector.LocalOffset = SocketTransform;
				ModuleConnector.bOverrideOffset = true;
				ModuleConnector.Polarity = ConnectorComp->Polarity;
				ModuleConnector.Priority = ConnectorComp->Priority;
				ModuleConnector.SpawnCapacity = ConnectorComp->SpawnCapacity;
				ModuleConnector.ConstraintOverrides = ConnectorComp->ConstraintOverrides;
				ModuleConnector.Priority = ConnectorComp->Priority;
				ModuleConnector.SpawnCapacity = ConnectorComp->SpawnCapacity;
				ModuleConnector.OverrideMode = ConnectorComp->OverrideMode;
				ModuleConnector.bManualOrbitalOverride = ConnectorComp->bManualOrbitalOverride;
				ModuleConnector.ManualOrbitalIndex = ConnectorComp->ManualOrbitalIndex;
				ModuleConnector.OrbitalIndex = -1;

				Data.Connectors.Add(ModuleConnector);
			}
		}

		const int32 ComponentCount = ConnectorComponents.Num();
		PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': %d connectors total (%d auto-extracted, %d inherited, %d from components)",
			*Cage->GetCageDisplayName(), Data.Connectors.Num(), AutoExtractedCount, InheritedCount, ComponentCount);

		// Compute orbital mask from connections
		const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
		PCGEX_VALENCY_VERBOSE(Building, "  Cage '%s': %d assets, %d orbitals",
			*Cage->GetCageDisplayName(), Data.AssetEntries.Num(), Orbitals.Num());

		for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
		{
			if (!Orbital.bEnabled)
			{
				PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': DISABLED", Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
				continue;
			}

			// Only count connected orbitals (or null cage connections if enabled)
			if (const APCGExValencyCageBase* ConnectedCage = Orbital.GetDisplayConnection())
			{
				// Check if it's a null cage (placeholder) - handle based on mode
				// See Orbital_Bitmask_Reference.md for mask behavior per mode
				if (ConnectedCage->IsNullCage())
				{
					if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(ConnectedCage))
					{
						switch (NullCage->GetPlaceholderMode())
						{
						case EPCGExPlaceholderMode::Boundary:
							// Boundary: Do NOT set OrbitalMask bit (tracked via BoundaryMask in BuildNeighborRelationships)
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': BOUNDARY (null cage) - tracked as boundary, not in OrbitalMask",
								Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
							break;

						case EPCGExPlaceholderMode::Wildcard:
							// Wildcard: SET OrbitalMask bit (tracked via WildcardMask in BuildNeighborRelationships)
							Data.OrbitalMask |= (1LL << Orbital.OrbitalIndex);
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': WILDCARD (null cage) - bit set",
								Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
							break;

						case EPCGExPlaceholderMode::Any:
							// Any: Do NOT set OrbitalMask bit (no constraint - pure spatial placeholder)
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': ANY (null cage) - no mask set, spatial placeholder only",
								Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
							break;
						}
					}
					else
					{
						// Fallback for legacy null cages without mode - treat as Boundary
						PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': NULL CAGE (legacy, treating as boundary)",
							Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
					}
				}
				else
				{
					// Regular connection - set the orbital bit
					Data.OrbitalMask |= (1LL << Orbital.OrbitalIndex);
					PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': Connected to '%s' - bit set",
						Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString(), *ConnectedCage->GetCageDisplayName());
				}
			}
			else
			{
				PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': NO CONNECTION", Orbital.OrbitalIndex, *Orbital.OrbitalName.ToString());
			}
		}

		// Log final orbital mask
		FString MaskBits;
		for (int32 Bit = 0; Bit < OrbitalSet->Num(); ++Bit)
		{
			MaskBits += (Data.OrbitalMask & (1LL << Bit)) ? TEXT("1") : TEXT("0");
		}
		PCGEX_VALENCY_VERBOSE(Building, "    -> Final OrbitalMask: %s (0x%llX)", *MaskBits, Data.OrbitalMask);

		OutCageData.Add(MoveTemp(Data));
	}

	VALENCY_LOG_SECTION(Building, "CAGE DATA COLLECTION COMPLETE");
	PCGEX_VALENCY_INFO(Building, "Valid cages: %d", OutCageData.Num());
}

void UPCGExValencyBondingRulesBuilder::PopulateAssembler(
	const TArray<FPCGExValencyCageData>& CageData,
	const UPCGExValencyOrbitalSet* OrbitalSet,
	FPCGExBondingRulesAssembler& OutAssembler)
{
	VALENCY_LOG_SECTION(Building, "POPULATING ASSEMBLER");

	// ========== Pass 1: Register modules from asset entries ==========

	// Map: CageData index -> array of assembler module indices for that cage
	TArray<TArray<int32>> CageDataToModuleIndices;
	CageDataToModuleIndices.SetNum(CageData.Num());

	// Track which modules have had metadata (properties/tags/connectors) set.
	// Only the first cage that creates a module sets metadata; subsequent dedup hits skip.
	TSet<int32> ModulesWithMetadata;

	// Track per-module asset-to-cage relative transforms (first entry per module wins)
	TMap<int32, FTransform> ModuleAssetTransforms;

	for (int32 CageIdx = 0; CageIdx < CageData.Num(); ++CageIdx)
	{
		const FPCGExValencyCageData& Data = CageData[CageIdx];

		for (const FPCGExValencyAssetEntry& Entry : Data.AssetEntries)
		{
			if (!Entry.IsValid())
			{
				continue;
			}

			// Build module desc
			FPCGExAssemblerModuleDesc Desc;
			Desc.Asset = Entry.Asset;
			Desc.AssetType = Entry.AssetType;
			Desc.OrbitalMask = Data.OrbitalMask;

			// Use entry-level settings if available (from mirror source), otherwise fall back to cage settings
			Desc.Settings = Entry.bHasSettings ? Entry.Settings : Data.Settings;
			Desc.PlacementPolicy = Data.PlacementPolicy;
			Desc.ModuleName = Data.ModuleName;

			if (Entry.bHasMaterialVariant)
			{
				Desc.MaterialVariant = Entry.MaterialVariant;
				Desc.bHasMaterialVariant = true;
			}

			// AddModule deduplicates by key
			const int32 ModuleIndex = OutAssembler.AddModule(Desc);
			CageDataToModuleIndices[CageIdx].AddUnique(ModuleIndex);

			// Capture asset-to-cage relative transform (first entry per module wins)
			if (!ModuleAssetTransforms.Contains(ModuleIndex))
			{
				FTransform AssetRelativeTransform = FTransform::Identity;
				if (AActor* Actor = Entry.SourceActor.Get())
				{
					if (APCGExValencyCage* Cage = Data.Cage.Get())
					{
						AssetRelativeTransform = Actor->GetActorTransform().GetRelativeTransform(Cage->GetActorTransform());
					}
				}
				ModuleAssetTransforms.Add(ModuleIndex, AssetRelativeTransform);
			}

			// Accumulate local transform if cage or entry preserves them
			// This happens for both new AND existing modules (transform accumulation)
			if (Data.bPreserveLocalTransforms || Entry.bPreserveLocalTransform)
			{
				OutAssembler.AddLocalTransform(ModuleIndex, Entry.LocalTransform);
			}
		}

		// Add properties, tags, and connectors only to modules that haven't had metadata set yet.
		// This matches the original Builder behavior: first cage to create a module sets metadata,
		// subsequent cages with the same module key only contribute transforms.
		for (const int32 ModuleIndex : CageDataToModuleIndices[CageIdx])
		{
			if (ModulesWithMetadata.Contains(ModuleIndex))
			{
				continue;
			}
			ModulesWithMetadata.Add(ModuleIndex);

			if (const FTransform* AssetXform = ModuleAssetTransforms.Find(ModuleIndex))
			{
				OutAssembler.SetAssetRelativeTransform(ModuleIndex, *AssetXform);
			}

			if (Data.ConnectorTransformStrategy.IsValid())
			{
				OutAssembler.SetConnectorTransformStrategy(ModuleIndex, Data.ConnectorTransformStrategy);
			}

			for (const FInstancedStruct& Prop : Data.Properties)
			{
				OutAssembler.AddProperty(ModuleIndex, Prop);
			}

			for (const FName& Tag : Data.Tags)
			{
				OutAssembler.AddTag(ModuleIndex, Tag);
			}

			for (const FPCGExValencyModuleConnector& Connector : Data.Connectors)
			{
				OutAssembler.AddConnector(ModuleIndex, Connector);
			}
		}

		PCGEX_VALENCY_VERBOSE(Building, "  CageData[%d]: %d modules registered [%s]",
			CageIdx, CageDataToModuleIndices[CageIdx].Num(),
			*FString::JoinBy(CageDataToModuleIndices[CageIdx], TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));
	}

	PCGEX_VALENCY_INFO(Building, "Pass 1 complete: %d modules registered", OutAssembler.GetModuleCount());

	// ========== Pass 2: Set neighbor relationships from orbital connections ==========

	VALENCY_LOG_SECTION(Building, "BUILDING NEIGHBOR RELATIONSHIPS (via Assembler)");

	// Build a cage pointer to cage data index map for fast lookup
	TMap<APCGExValencyCage*, int32> CageToDataIndex;
	for (int32 i = 0; i < CageData.Num(); ++i)
	{
		if (APCGExValencyCage* Cage = CageData[i].Cage.Get())
		{
			CageToDataIndex.Add(Cage, i);
		}
	}

	for (int32 CageIdx = 0; CageIdx < CageData.Num(); ++CageIdx)
	{
		const FPCGExValencyCageData& Data = CageData[CageIdx];
		APCGExValencyCage* Cage = Data.Cage.Get();
		if (!Cage)
		{
			continue;
		}

		const TArray<int32>& CageModuleIndices = CageDataToModuleIndices[CageIdx];
		const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();

		PCGEX_VALENCY_VERBOSE(Building, "  Processing cage '%s' (modules: [%s]):",
			*Cage->GetCageDisplayName(),
			*FString::JoinBy(CageModuleIndices, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));

		for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
		{
			if (!Orbital.bEnabled || Orbital.OrbitalIndex < 0)
			{
				continue;
			}

			// Get orbital name
			FName OrbitalName = Orbital.OrbitalName;
			if (OrbitalName.IsNone() && OrbitalSet->IsValidIndex(Orbital.OrbitalIndex))
			{
				OrbitalName = OrbitalSet->Orbitals[Orbital.OrbitalIndex].GetOrbitalName();
			}

			if (const APCGExValencyCageBase* ConnectedBase = Orbital.GetDisplayConnection())
			{
				// Handle null cages (placeholders) based on mode
				if (ConnectedBase->IsNullCage())
				{
					if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(ConnectedBase))
					{
						switch (NullCage->GetPlaceholderMode())
						{
						case EPCGExPlaceholderMode::Boundary:
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': BOUNDARY (null cage)",
								Orbital.OrbitalIndex, *OrbitalName.ToString());
							for (const int32 ModuleIndex : CageModuleIndices)
							{
								OutAssembler.SetBoundaryOrbital(ModuleIndex, Orbital.OrbitalIndex);
							}
							break;

						case EPCGExPlaceholderMode::Wildcard:
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': WILDCARD (null cage)",
								Orbital.OrbitalIndex, *OrbitalName.ToString());
							for (const int32 ModuleIndex : CageModuleIndices)
							{
								OutAssembler.SetWildcardOrbital(ModuleIndex, Orbital.OrbitalIndex);
							}
							break;

						case EPCGExPlaceholderMode::Any:
							PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': ANY (null cage) - no constraint",
								Orbital.OrbitalIndex, *OrbitalName.ToString());
							break;
						}
					}
					else
					{
						// Fallback for legacy null cages without mode - treat as Boundary
						PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': BOUNDARY (legacy null cage)",
							Orbital.OrbitalIndex, *OrbitalName.ToString());
						for (const int32 ModuleIndex : CageModuleIndices)
						{
							OutAssembler.SetBoundaryOrbital(ModuleIndex, Orbital.OrbitalIndex);
						}
					}
				}
				else if (const APCGExValencyCage* ConnectedCage = Cast<APCGExValencyCage>(ConnectedBase))
				{
					// Get connected cage's module indices
					TArray<int32> NeighborModuleIndices;
					if (const int32* ConnectedDataIndex = CageToDataIndex.Find(const_cast<APCGExValencyCage*>(ConnectedCage)))
					{
						NeighborModuleIndices = CageDataToModuleIndices[*ConnectedDataIndex];
					}

					PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': Connected to '%s', neighbor modules: [%s]",
						Orbital.OrbitalIndex, *OrbitalName.ToString(), *ConnectedCage->GetCageDisplayName(),
						*FString::JoinBy(NeighborModuleIndices, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));

					// Add neighbors to each of this cage's modules
					for (const int32 ModuleIndex : CageModuleIndices)
					{
						OutAssembler.AddNeighbors(ModuleIndex, OrbitalName, NeighborModuleIndices);
					}
				}
			}
			else
			{
				// No explicit connection - apply MissingConnectionBehavior if configured
				switch (Cage->MissingConnectionBehavior)
				{
				case EPCGExMissingConnectionBehavior::Unconstrained:
					PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': NO CONNECTION (unconstrained)",
						Orbital.OrbitalIndex, *OrbitalName.ToString());
					break;

				case EPCGExMissingConnectionBehavior::Boundary:
					PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': NO CONNECTION -> BOUNDARY (via MissingConnectionBehavior)",
						Orbital.OrbitalIndex, *OrbitalName.ToString());
					for (const int32 ModuleIndex : CageModuleIndices)
					{
						OutAssembler.SetBoundaryOrbital(ModuleIndex, Orbital.OrbitalIndex);
					}
					break;

				case EPCGExMissingConnectionBehavior::Wildcard:
					PCGEX_VALENCY_VERBOSE(Building, "    Orbital[%d] '%s': NO CONNECTION -> WILDCARD (via MissingConnectionBehavior)",
						Orbital.OrbitalIndex, *OrbitalName.ToString());
					for (const int32 ModuleIndex : CageModuleIndices)
					{
						OutAssembler.SetWildcardOrbital(ModuleIndex, Orbital.OrbitalIndex);
					}
					break;
				}
			}
		}
	}

	VALENCY_LOG_SECTION(Building, "ASSEMBLER POPULATION COMPLETE");
}

TArray<FPCGExValencyAssetEntry> UPCGExValencyBondingRulesBuilder::GetEffectiveAssetEntries(const APCGExValencyCage* Cage)
{
	if (!Cage)
	{
		return {};
	}

	TArray<FPCGExValencyAssetEntry> AllEntries;

	// Start with cage's own assets
	const TArray<FPCGExValencyAssetEntry> OwnAssets = Cage->GetAllAssetEntries();
	AllEntries.Append(OwnAssets);

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveAssetEntries for '%s': %d own assets, %d mirror sources",
		*Cage->GetCageDisplayName(), OwnAssets.Num(), Cage->MirrorSources.Num());

	// If no mirror sources, return early
	if (Cage->MirrorSources.Num() == 0)
	{
		return AllEntries;
	}

	// Get this cage's rotation for applying to mirrored local transforms
	const FQuat CageRotation = Cage->GetActorQuat();

	// Track visited sources to prevent infinite recursion
	TSet<const AActor*> VisitedSources;
	VisitedSources.Add(Cage);

	// Lambda to collect entries from a source with per-type flags
	TFunction<void(AActor*, uint8, uint8)> CollectFromSource = [&](AActor* Source, uint8 MirrorFlags, uint8 RecursiveFlags)
	{
		if (!Source)
		{
			PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source: NULL - skipping");
			return;
		}
		if (VisitedSources.Contains(Source))
		{
			PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source '%s': already visited - skipping (cycle prevention)", *Source->GetName());
			return;
		}
		VisitedSources.Add(Source);
		// Skip disabled source cages
		if (const APCGExValencyCageBase* SourceCageBase = Cast<APCGExValencyCageBase>(Source))
		{
			if (!SourceCageBase->bEnabledForCompilation) return;
		}

		// Only collect assets if Assets flag is set
		if (!(MirrorFlags & static_cast<uint8>(EPCGExMirrorContent::Assets)))
		{
			// Still need to recurse for nested sources even if we don't collect assets here
			if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
			{
				if (SourceCage->MirrorSources.Num() > 0)
				{
					for (const FPCGExMirrorSource& NestedEntry : SourceCage->MirrorSources)
					{
						if (!NestedEntry.IsValid()) continue;
						const uint8 ChildMirror = RecursiveFlags & NestedEntry.MirrorFlags;
						const uint8 ChildRecurse = RecursiveFlags & NestedEntry.RecursiveFlags;
						if (ChildMirror & static_cast<uint8>(EPCGExMirrorContent::Assets))
						{
							CollectFromSource(NestedEntry.Source, ChildMirror, ChildRecurse);
						}
					}
				}
			}
			return;
		}

		TArray<FPCGExValencyAssetEntry> SourceEntries;

		// Check if it's a cage
		if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
		{
			SourceEntries = SourceCage->GetAllAssetEntries();
			PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source CAGE '%s': %d assets", *SourceCage->GetCageDisplayName(), SourceEntries.Num());

			// Recursively collect from cage's mirror sources
			if (SourceCage->MirrorSources.Num() > 0)
			{
				PCGEX_VALENCY_VERBOSE(Mirror, "      Recursing into %d nested mirror sources", SourceCage->MirrorSources.Num());
				for (const FPCGExMirrorSource& NestedEntry : SourceCage->MirrorSources)
				{
					if (!NestedEntry.IsValid()) continue;
					const uint8 ChildMirror = RecursiveFlags & NestedEntry.MirrorFlags;
					const uint8 ChildRecurse = RecursiveFlags & NestedEntry.RecursiveFlags;
					if (ChildMirror & static_cast<uint8>(EPCGExMirrorContent::Assets))
					{
						CollectFromSource(NestedEntry.Source, ChildMirror, ChildRecurse);
					}
				}
			}
		}
		// Check if it's an asset palette
		else if (const APCGExValencyAssetPalette* SourcePalette = Cast<APCGExValencyAssetPalette>(Source))
		{
			SourceEntries = SourcePalette->GetAllAssetEntries();
			PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source PALETTE '%s': %d assets", *SourcePalette->GetPaletteDisplayName(), SourceEntries.Num());
		}
		else
		{
			PCGEX_VALENCY_WARNING(Mirror, "    Mirror source '%s': INVALID TYPE '%s' - not a Cage or Palette, skipping",
				*Source->GetName(), *Source->GetClass()->GetName());
			return;
		}

		// Apply cage rotation to mirrored local transforms and add to results
		for (FPCGExValencyAssetEntry& Entry : SourceEntries)
		{
			if (!Entry.LocalTransform.Equals(FTransform::Identity, 0.1f))
			{
				// Rotate the source's local offset by this cage's rotation
				const FVector RotatedOffset = CageRotation.RotateVector(Entry.LocalTransform.GetTranslation());
				const FQuat CombinedRotation = CageRotation * Entry.LocalTransform.GetRotation();

				Entry.LocalTransform.SetTranslation(RotatedOffset);
				Entry.LocalTransform.SetRotation(CombinedRotation);
			}

			AllEntries.Add(Entry);
		}
	};

	// Collect from all mirror sources
	for (const FPCGExMirrorSource& Entry : Cage->MirrorSources)
	{
		if (!Entry.IsValid()) continue;
		CollectFromSource(Entry.Source, Entry.MirrorFlags, Entry.RecursiveFlags);
	}

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveAssetEntries for '%s': TOTAL %d assets (after mirror resolution)",
		*Cage->GetCageDisplayName(), AllEntries.Num());

	return AllEntries;
}

TArray<FInstancedStruct> UPCGExValencyBondingRulesBuilder::GetEffectiveProperties(const APCGExValencyCage* Cage)
{
	if (!Cage)
	{
		return {};
	}

	TArray<FInstancedStruct> AllProperties;

	// Helper to collect properties from an actor's property collection component
	auto CollectPropertiesFromActor = [](const AActor* Actor, TArray<FInstancedStruct>& OutProperties)
	{
		if (!Actor)
		{
			return;
		}

		// Find property collection component
		UPCGExPropertyCollectionComponent* PropCollectionComp = Actor->FindComponentByClass<UPCGExPropertyCollectionComponent>();
		if (!PropCollectionComp)
		{
			return;
		}

		// Sync PropertyName and HeaderId for all schemas
		for (FPCGExPropertySchema& Schema : PropCollectionComp->GetPropertiesMutable().Schemas)
		{
			Schema.SyncPropertyName();
		}

		// Build properties from schema
		TArray<FInstancedStruct> CompiledProperties = PropCollectionComp->GetProperties().BuildSchema();
		OutProperties.Append(MoveTemp(CompiledProperties));
	};

	// Start with cage's own properties
	CollectPropertiesFromActor(Cage, AllProperties);
	const int32 OwnPropertyCount = AllProperties.Num();

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveProperties for '%s': %d own properties, %d mirror sources",
		*Cage->GetCageDisplayName(), OwnPropertyCount, Cage->MirrorSources.Num());

	// If no mirror sources, return early
	if (Cage->MirrorSources.Num() == 0)
	{
		return AllProperties;
	}

	// Track visited sources to prevent infinite recursion
	TSet<const AActor*> VisitedSources;
	VisitedSources.Add(Cage);

	// Lambda to collect properties from a source with per-type flags
	TFunction<void(AActor*, uint8, uint8)> CollectFromSource = [&](AActor* Source, uint8 MirrorFlags, uint8 RecursiveFlags)
	{
		if (!Source)
		{
			return;
		}
		if (VisitedSources.Contains(Source))
		{
			return; // Cycle prevention
		}
		VisitedSources.Add(Source);
		// Skip disabled source cages
		if (const APCGExValencyCageBase* SourceCageBase = Cast<APCGExValencyCageBase>(Source))
		{
			if (!SourceCageBase->bEnabledForCompilation) return;
		}

		// Only collect properties if Properties flag is set
		if (MirrorFlags & static_cast<uint8>(EPCGExMirrorContent::Properties))
		{
			// Check if it's a cage
			if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
			{
				CollectPropertiesFromActor(SourceCage, AllProperties);
				PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source CAGE '%s': collecting properties", *SourceCage->GetCageDisplayName());
			}
			// Check if it's an asset palette
			else if (const APCGExValencyAssetPalette* SourcePalette = Cast<APCGExValencyAssetPalette>(Source))
			{
				CollectPropertiesFromActor(SourcePalette, AllProperties);
				PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source PALETTE '%s': collecting properties", *SourcePalette->GetPaletteDisplayName());
			}
		}

		// Recurse into nested mirror sources if this is a cage
		if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
		{
			if (SourceCage->MirrorSources.Num() > 0)
			{
				for (const FPCGExMirrorSource& NestedEntry : SourceCage->MirrorSources)
				{
					if (!NestedEntry.IsValid()) continue;
					const uint8 ChildMirror = RecursiveFlags & NestedEntry.MirrorFlags;
					const uint8 ChildRecurse = RecursiveFlags & NestedEntry.RecursiveFlags;
					if (ChildMirror & static_cast<uint8>(EPCGExMirrorContent::Properties))
					{
						CollectFromSource(NestedEntry.Source, ChildMirror, ChildRecurse);
					}
				}
			}
		}
	};

	// Collect from all mirror sources
	for (const FPCGExMirrorSource& Entry : Cage->MirrorSources)
	{
		if (!Entry.IsValid()) continue;
		CollectFromSource(Entry.Source, Entry.MirrorFlags, Entry.RecursiveFlags);
	}

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveProperties for '%s': TOTAL %d properties (after mirror resolution)",
		*Cage->GetCageDisplayName(), AllProperties.Num());

	return AllProperties;
}

TArray<FName> UPCGExValencyBondingRulesBuilder::GetEffectiveTags(const APCGExValencyCage* Cage)
{
	if (!Cage)
	{
		return {};
	}

	TArray<FName> AllTags;

	// Start with cage's own actor tags
	AllTags.Append(Cage->Tags);
	const int32 OwnTagCount = AllTags.Num();

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveTags for '%s': %d own tags, %d mirror sources",
		*Cage->GetCageDisplayName(), OwnTagCount, Cage->MirrorSources.Num());

	// If no mirror sources, return early
	if (Cage->MirrorSources.Num() == 0)
	{
		return AllTags;
	}

	// Track visited sources to prevent infinite recursion
	TSet<const AActor*> VisitedSources;
	VisitedSources.Add(Cage);

	// Lambda to collect tags from a source with per-type flags
	TFunction<void(AActor*, uint8, uint8)> CollectFromSource = [&](AActor* Source, uint8 MirrorFlags, uint8 RecursiveFlags)
	{
		if (!Source)
		{
			return;
		}
		if (VisitedSources.Contains(Source))
		{
			return; // Cycle prevention
		}
		VisitedSources.Add(Source);
		// Skip disabled source cages
		if (const APCGExValencyCageBase* SourceCageBase = Cast<APCGExValencyCageBase>(Source))
		{
			if (!SourceCageBase->bEnabledForCompilation) return;
		}

		// Only collect tags if Tags flag is set
		if (MirrorFlags & static_cast<uint8>(EPCGExMirrorContent::Tags))
		{
			// Collect actor tags from source
			for (const FName& Tag : Source->Tags)
			{
				AllTags.AddUnique(Tag);
			}

			// Check if it's a cage - log
			if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
			{
				PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source CAGE '%s': collecting %d tags", *SourceCage->GetCageDisplayName(), SourceCage->Tags.Num());
			}
			else if (const APCGExValencyAssetPalette* SourcePalette = Cast<APCGExValencyAssetPalette>(Source))
			{
				PCGEX_VALENCY_VERBOSE(Mirror, "    Mirror source PALETTE '%s': collecting %d tags", *SourcePalette->GetPaletteDisplayName(), SourcePalette->Tags.Num());
			}
		}

		// Recurse into nested mirror sources if this is a cage
		if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(Source))
		{
			if (SourceCage->MirrorSources.Num() > 0)
			{
				for (const FPCGExMirrorSource& NestedEntry : SourceCage->MirrorSources)
				{
					if (!NestedEntry.IsValid()) continue;
					const uint8 ChildMirror = RecursiveFlags & NestedEntry.MirrorFlags;
					const uint8 ChildRecurse = RecursiveFlags & NestedEntry.RecursiveFlags;
					if (ChildMirror & static_cast<uint8>(EPCGExMirrorContent::Tags))
					{
						CollectFromSource(NestedEntry.Source, ChildMirror, ChildRecurse);
					}
				}
			}
		}
	};

	// Collect from all mirror sources
	for (const FPCGExMirrorSource& Entry : Cage->MirrorSources)
	{
		if (!Entry.IsValid()) continue;
		CollectFromSource(Entry.Source, Entry.MirrorFlags, Entry.RecursiveFlags);
	}

	PCGEX_VALENCY_VERBOSE(Mirror, "  GetEffectiveTags for '%s': TOTAL %d tags (after mirror resolution)",
		*Cage->GetCageDisplayName(), AllTags.Num());

	return AllTags;
}

FString UPCGExValencyBondingRulesBuilder::GenerateVariantName(
	const FPCGExValencyAssetEntry& Entry,
	int64 OrbitalMask,
	bool bHasLocalTransform)
{
	// Get asset name
	FString AssetName = Entry.Asset.GetAssetName();
	if (AssetName.IsEmpty())
	{
		AssetName = TEXT("Unknown");
	}

	// Count connected orbitals for connectivity info
	int32 ConnectionCount = 0;
	for (int32 i = 0; i < 64; ++i)
	{
		if (OrbitalMask & (1LL << i))
		{
			++ConnectionCount;
		}
	}

	FString VariantName = FString::Printf(TEXT("%s_%dconn"), *AssetName, ConnectionCount);

	// Add transform indicator if present
	if (bHasLocalTransform)
	{
		const FVector Loc = Entry.LocalTransform.GetLocation();
		// Add simplified position indicator (e.g., "NE" for northeast corner)
		FString PosIndicator;
		if (FMath::Abs(Loc.X) > 1.0f || FMath::Abs(Loc.Y) > 1.0f)
		{
			if (Loc.X > 1.0f) PosIndicator += TEXT("E");
			else if (Loc.X < -1.0f) PosIndicator += TEXT("W");
			if (Loc.Y > 1.0f) PosIndicator += TEXT("N");
			else if (Loc.Y < -1.0f) PosIndicator += TEXT("S");
			if (Loc.Z > 1.0f) PosIndicator += TEXT("U");
			else if (Loc.Z < -1.0f) PosIndicator += TEXT("D");
		}
		if (!PosIndicator.IsEmpty())
		{
			VariantName += TEXT("_") + PosIndicator;
		}
		else
		{
			VariantName += TEXT("_offset");
		}
	}

	return VariantName;
}

void UPCGExValencyBondingRulesBuilder::DiscoverMaterialVariants(
	const TArray<FPCGExValencyCageData>& CageData,
	UPCGExValencyBondingRules* TargetRules)
{
	if (!TargetRules)
	{
		return;
	}

	// Clear previous discoveries
	TargetRules->DiscoveredMaterialVariants.Empty();

	// Collect material variants from all cages
	// Variants are discovered during cage scanning, we just need to merge them
	for (const FPCGExValencyCageData& Data : CageData)
	{
		APCGExValencyCage* Cage = Data.Cage.Get();
		if (!Cage)
		{
			continue;
		}

		const TMap<FSoftObjectPath, TArray<FPCGExValencyMaterialVariant>>& CageVariants = Cage->GetDiscoveredMaterialVariants();

		// Merge cage variants into target rules
		for (const auto& Pair : CageVariants)
		{
			const FSoftObjectPath& MeshPath = Pair.Key;
			const TArray<FPCGExValencyMaterialVariant>& CageVariantList = Pair.Value;

			TArray<FPCGExValencyMaterialVariant>& TargetVariants = TargetRules->DiscoveredMaterialVariants.FindOrAdd(MeshPath);

			for (const FPCGExValencyMaterialVariant& CageVariant : CageVariantList)
			{
				// Check if this exact configuration already exists in target
				bool bFound = false;
				for (FPCGExValencyMaterialVariant& ExistingVariant : TargetVariants)
				{
					if (ExistingVariant == CageVariant)
					{
						// Merge discovery counts
						ExistingVariant.DiscoveryCount += CageVariant.DiscoveryCount;
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					TargetVariants.Add(CageVariant);
				}
			}
		}
	}
}

void UPCGExValencyBondingRulesBuilder::CompilePatterns(
	const TArray<AValencyContextVolume*>& Volumes,
	const TMap<FString, int32>& ModuleKeyToIndex,
	UPCGExValencyBondingRules* TargetRules,
	const UPCGExValencyOrbitalSet* OrbitalSet,
	FPCGExValencyBuildResult& OutResult)
{
	if (!TargetRules || !OrbitalSet)
	{
		return;
	}

	VALENCY_LOG_SECTION(Building, "COMPILING PATTERNS");

	// Clear existing patterns
	TargetRules->Patterns.Patterns.Empty();
	TargetRules->Patterns.ExclusivePatternIndices.Empty();
	TargetRules->Patterns.AdditivePatternIndices.Empty();

	// Collect all pattern cages from all volumes
	TArray<APCGExValencyCagePattern*> AllPatternCages;
	TSet<APCGExValencyCagePattern*> ProcessedRoots;

	for (AValencyContextVolume* Volume : Volumes)
	{
		if (!Volume || Volume->GetBondingRules() != TargetRules)
		{
			continue;
		}

		TArray<APCGExValencyCageBase*> VolumeCages;
		Volume->CollectContainedCages(VolumeCages);

		for (APCGExValencyCageBase* CageBase : VolumeCages)
		{
			if (APCGExValencyCagePattern* PatternCage = Cast<APCGExValencyCagePattern>(CageBase))
			{
				AllPatternCages.AddUnique(PatternCage);
			}
		}
	}

	PCGEX_VALENCY_INFO(Building, "Found %d pattern cages across %d volumes", AllPatternCages.Num(), Volumes.Num());

	if (AllPatternCages.Num() == 0)
	{
		VALENCY_LOG_SECTION(Building, "NO PATTERNS TO COMPILE");
		return;
	}

	// CRITICAL FIX: Refresh ALL pattern cages BEFORE compiling ANY pattern.
	// This ensures we use current connection data, not stale pointers.
	// The three-pass refresh in CompileSinglePattern is insufficient because it only
	// refreshes cages in the current network - if a cage moved OUT of the network,
	// it won't be refreshed and remaining cages might have stale references.
	for (APCGExValencyCagePattern* PatternCage : AllPatternCages)
	{
		if (PatternCage)
		{
			PatternCage->DetectNearbyConnections();
		}
	}

	// Find all pattern roots and compile each pattern
	for (APCGExValencyCagePattern* PatternCage : AllPatternCages)
	{
		if (!PatternCage || !PatternCage->bIsPatternRoot)
		{
			continue;
		}

		if (ProcessedRoots.Contains(PatternCage))
		{
			continue;
		}
		
		ProcessedRoots.Add(PatternCage);

		PCGEX_VALENCY_VERBOSE(Building, "Compiling pattern from root '%s'", *PatternCage->GetCageDisplayName());

		FPCGExValencyPatternCompiled CompiledPattern;
		if (CompileSinglePattern(PatternCage, ModuleKeyToIndex, TargetRules, OrbitalSet, CompiledPattern, OutResult))
		{
			const int32 PatternIndex = TargetRules->Patterns.Patterns.Num();
			TargetRules->Patterns.Patterns.Add(MoveTemp(CompiledPattern));

			// Sort into exclusive vs additive
			if (CompiledPattern.Settings.bExclusive)
			{
				TargetRules->Patterns.ExclusivePatternIndices.Add(PatternIndex);
			}
			else
			{
				TargetRules->Patterns.AdditivePatternIndices.Add(PatternIndex);
			}

			PCGEX_VALENCY_INFO(Building, "  Pattern '%s' compiled: %d entries, %d active",
				*CompiledPattern.Settings.PatternName.ToString(),
				CompiledPattern.Entries.Num(),
				CompiledPattern.ActiveEntryCount);
		}
	}

	OutResult.PatternCount = TargetRules->Patterns.Patterns.Num();

	VALENCY_LOG_SECTION(Building, "PATTERN COMPILATION COMPLETE");
	PCGEX_VALENCY_INFO(Building, "Total patterns: %d (%d exclusive, %d additive)",
		OutResult.PatternCount,
		TargetRules->Patterns.ExclusivePatternIndices.Num(),
		TargetRules->Patterns.AdditivePatternIndices.Num());
}

bool UPCGExValencyBondingRulesBuilder::CompileSinglePattern(
	APCGExValencyCagePattern* RootCage,
	const TMap<FString, int32>& ModuleKeyToIndex,
	UPCGExValencyBondingRules* TargetRules,
	const UPCGExValencyOrbitalSet* OrbitalSet,
	FPCGExValencyPatternCompiled& OutPattern,
	FPCGExValencyBuildResult& OutResult)
{
	if (!RootCage || !TargetRules || !OrbitalSet)
	{
		return false;
	}

	// CRITICAL: Refresh connections for all pattern cages in the network BEFORE traversing.
	// This ensures the network traversal uses up-to-date orbital data.
	// Without this, cages outside the volume or beyond probe radius might have stale connections.
	//
	// Pass 1: Get initial network (might include stale connections to moved cages)
	TArray<APCGExValencyCagePattern*> InitialNetwork = RootCage->GetConnectedPatternCages();

	// Pass 2: Refresh connections for ALL cages in the initial network
	for (APCGExValencyCagePattern* PatternCage : InitialNetwork)
	{
		if (PatternCage)
		{
			PatternCage->DetectNearbyConnections();
		}
	}

	// Pass 3: Get the UPDATED network with fresh orbital data
	TArray<APCGExValencyCagePattern*> ConnectedCages = RootCage->GetConnectedPatternCages();

	if (ConnectedCages.Num() == 0)
	{
		OutResult.Warnings.Add(FText::Format(
			LOCTEXT("PatternNoCages", "Pattern root '{0}' has no connected cages."),
			FText::FromString(RootCage->GetCageDisplayName())
		));
		return false;
	}

	// Build cage to entry index mapping (root is always entry 0)
	TMap<APCGExValencyCagePattern*, int32> CageToEntryIndex;
	CageToEntryIndex.Add(RootCage, 0);

	int32 NextEntryIndex = 1;
	for (APCGExValencyCagePattern* Cage : ConnectedCages)
	{
		if (Cage != RootCage && !CageToEntryIndex.Contains(Cage))
		{
			CageToEntryIndex.Add(Cage, NextEntryIndex++);
		}
	}

	// Allocate entries
	OutPattern.Entries.SetNum(CageToEntryIndex.Num());
	OutPattern.ActiveEntryCount = 0;

	// Copy settings from root
	const FPCGExValencyPatternSettings& RootSettings = RootCage->PatternSettings;
	OutPattern.Settings.PatternName = RootSettings.PatternName;
	OutPattern.Settings.Weight = RootSettings.Weight;
	OutPattern.Settings.MinMatches = RootSettings.MinMatches;
	OutPattern.Settings.MaxMatches = RootSettings.MaxMatches;
	OutPattern.Settings.bExclusive = RootSettings.bExclusive;
	OutPattern.Settings.OutputStrategy = RootSettings.OutputStrategy;
	OutPattern.Settings.TransformMode = RootSettings.TransformMode;
	OutPattern.ReplacementAsset = RootSettings.ReplacementAsset;

	// Copy actor tags from root cage for pattern filtering
	OutPattern.Settings.Tags = RootCage->Tags;

	// Store root cage transform for rotated pattern matching
	OutPattern.RootTransform = RootCage->GetActorTransform();

	// Resolve SwapToModuleName to module index
	if (OutPattern.Settings.OutputStrategy == EPCGExPatternOutputStrategy::Swap && !RootSettings.SwapToModuleName.IsNone())
	{
		OutPattern.SwapTargetModuleIndex = -1;
		for (int32 ModuleIndex = 0; ModuleIndex < TargetRules->Modules.Num(); ++ModuleIndex)
		{
			if (TargetRules->Modules[ModuleIndex].ModuleName == RootSettings.SwapToModuleName)
			{
				OutPattern.SwapTargetModuleIndex = ModuleIndex;
				break;
			}
		}

		if (OutPattern.SwapTargetModuleIndex < 0)
		{
			OutResult.Warnings.Add(FText::Format(
				LOCTEXT("SwapTargetNotFound", "Pattern '{0}': Swap target module '{1}' not found."),
				FText::FromName(RootSettings.PatternName),
				FText::FromName(RootSettings.SwapToModuleName)
			));
		}
	}

	// Compile each entry
	for (const auto& Pair : CageToEntryIndex)
	{
		APCGExValencyCagePattern* Cage = Pair.Key;
		const int32 EntryIndex = Pair.Value;

		if (!Cage)
		{
			continue;
		}

		FPCGExValencyPatternEntryCompiled& Entry = OutPattern.Entries[EntryIndex];

		// Copy flags
		Entry.bIsActive = Cage->bIsActiveInPattern;

		if (Entry.bIsActive)
		{
			OutPattern.ActiveEntryCount++;
		}

		// Resolve proxied cages to module indices
		// KEY INSIGHT: The PATTERN CAGE defines the TOPOLOGY (orbital connections),
		// while the PROXIED CAGE defines the ASSET to match.
		// We need to compute the orbital mask from the PATTERN CAGE, not the proxied cage!
		{
			// Compute orbital mask from the PATTERN CAGE's connections (not the proxied cage!)
			// Include all real connections (pattern cages) and null cages in Wildcard mode
			// See Orbital_Bitmask_Reference.md: Wildcard ⊆ OrbitalMask, Boundary ∩ OrbitalMask == ∅
			const TArray<FPCGExValencyCageOrbital>& PatternOrbitals = Cage->GetOrbitals();
			int64 PatternOrbitalMask = 0;
			for (const FPCGExValencyCageOrbital& Orbital : PatternOrbitals)
			{
				if (!Orbital.bEnabled) continue;

				if (const APCGExValencyCageBase* Connection = Orbital.GetDisplayConnection())
				{
					if (Connection->IsNullCage())
					{
						// Only include if Wildcard mode (Boundary and Any are NOT in OrbitalMask)
						if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(Connection))
						{
							if (NullCage->IsWildcardMode())
							{
								PatternOrbitalMask |= (1LL << Orbital.OrbitalIndex);
							}
						}
					}
					else
					{
						// Regular connection (pattern cage) - include in mask
						PatternOrbitalMask |= (1LL << Orbital.OrbitalIndex);
					}
				}
			}

			for (const TObjectPtr<APCGExValencyCage>& ProxiedCage : Cage->ProxiedCages)
			{
				if (!ProxiedCage) { continue; }

				// Get all asset entries from the proxied cage
				TArray<FPCGExValencyAssetEntry> ProxiedEntries = ProxiedCage->GetAllAssetEntries();

				for (const FPCGExValencyAssetEntry& ProxiedEntry : ProxiedEntries)
				{
					if (!ProxiedEntry.IsValid())
					{
						continue;
					}

					const FSoftObjectPath AssetPath = ProxiedEntry.Asset.ToSoftObjectPath();
					// Check both cage-level and entry-level preserve flags
					const FTransform* TransformPtr = (ProxiedCage->bPreserveLocalTransforms || ProxiedEntry.bPreserveLocalTransform) ? &ProxiedEntry.LocalTransform : nullptr;
					const FPCGExValencyMaterialVariant* MaterialVariantPtr = ProxiedEntry.bHasMaterialVariant ? &ProxiedEntry.MaterialVariant : nullptr;

					// Find all modules that match by ASSET only.
					// The pattern cage's orbital topology defines the ADJACENCY structure for matching,
					// NOT a filter on which modules can be used. The runtime matcher checks if
					// actual cluster connectivity matches the pattern's adjacency.
					int32 MatchCount = 0;
					for (int32 ModuleIndex = 0; ModuleIndex < TargetRules->Modules.Num(); ++ModuleIndex)
					{
						const FPCGExValencyModuleDefinition& Module = TargetRules->Modules[ModuleIndex];

						// Check asset match
						if (Module.Asset.ToSoftObjectPath() != AssetPath)
						{
							continue;
						}

						// Check transform match (if pattern cage preserves transforms)
						// Check if ANY of the module's transforms matches
						if (TransformPtr)
						{
							if (!Module.bHasLocalTransform)
							{
								continue;
							}
							bool bFoundMatchingTransform = false;
							for (const FTransform& ModuleTransform : Module.LocalTransforms)
							{
								if (ModuleTransform.Equals(*TransformPtr, 0.1f))
								{
									bFoundMatchingTransform = true;
									break;
								}
							}
							if (!bFoundMatchingTransform)
							{
								continue;
							}
						}

						// Check material variant match (if entry has material variant)
						if (MaterialVariantPtr)
						{
							if (!Module.bHasMaterialVariant)
							{
								continue;
							}
							if (Module.MaterialVariant.Overrides.Num() != MaterialVariantPtr->Overrides.Num())
							{
								continue;
							}
							bool bMaterialMatch = true;
							for (int32 i = 0; i < MaterialVariantPtr->Overrides.Num() && bMaterialMatch; ++i)
							{
								if (Module.MaterialVariant.Overrides[i].SlotIndex != MaterialVariantPtr->Overrides[i].SlotIndex ||
									Module.MaterialVariant.Overrides[i].Material != MaterialVariantPtr->Overrides[i].Material)
								{
									bMaterialMatch = false;
								}
							}
							if (!bMaterialMatch)
							{
								continue;
							}
						}

						// NO orbital mask check here - the pattern's adjacency structure handles
						// connectivity constraints at runtime, not at compile time.
						Entry.ModuleIndices.AddUnique(ModuleIndex);
					}
				}
			}

			// Warn if no modules found but ProxiedCages were specified
			// (Empty ModuleIndices + empty ProxiedCages = intentional wildcard)
			if (Entry.ModuleIndices.IsEmpty() && Cage->ProxiedCages.Num() > 0)
			{
				OutResult.Warnings.Add(FText::Format(
					LOCTEXT("PatternEntryNoModules", "Pattern '{0}', entry from cage '{1}': No matching modules found for proxied cages."),
					FText::FromName(RootSettings.PatternName),
					FText::FromString(Cage->GetCageDisplayName())
				));
			}
		}

		// Build adjacency from orbital connections
		// IMPORTANT: We recompute orbital indices from spatial direction rather than trusting
		// the stored Orbital.OrbitalIndex, because manual connections or auto-detection bugs
		// could result in wrong orbital assignments.
		// NOTE: We must use the orbital set's bTransformDirection setting to match runtime
		// behavior in WriteValencyOrbitals, NOT the cage's ShouldTransformOrbitalDirections().
		const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
		const FVector CageLocation = Cage->GetActorLocation();
		const FTransform CageTransform = Cage->GetActorTransform();

		// Build orbital resolver for direction-to-index lookup
		PCGExValency::FOrbitalDirectionResolver OrbitalResolver;
		OrbitalResolver.BuildFrom(OrbitalSet);

		// Use the orbital set's transform setting to match runtime behavior
		const bool bUseTransform = OrbitalSet->bTransformDirection;

		for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
		{
			if (!Orbital.bEnabled || Orbital.OrbitalIndex < 0)
			{
				continue;
			}

			APCGExValencyCageBase* ConnectedBase = Orbital.GetDisplayConnection();
			if (!ConnectedBase)
			{
				continue;
			}

			// Check if connected to null cage (placeholder) - handle based on mode
			// See Orbital_Bitmask_Reference.md for mask behavior per mode
			if (ConnectedBase->IsNullCage())
			{
				if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(ConnectedBase))
				{
					switch (NullCage->GetPlaceholderMode())
					{
					case EPCGExPlaceholderMode::Boundary:
						Entry.BoundaryOrbitalMask |= (1ULL << Orbital.OrbitalIndex);
						break;

					case EPCGExPlaceholderMode::Wildcard:
						Entry.WildcardOrbitalMask |= (1ULL << Orbital.OrbitalIndex);
						break;

					case EPCGExPlaceholderMode::Any:
						// Any mode: No mask set - pure spatial placeholder with no runtime constraint
						break;
					}
				}
				else
				{
					// Legacy fallback - treat as boundary
					Entry.BoundaryOrbitalMask |= (1ULL << Orbital.OrbitalIndex);
				}
				continue;
			}

			// Check if connected to another pattern cage
			if (APCGExValencyCagePattern* ConnectedPattern = Cast<APCGExValencyCagePattern>(ConnectedBase))
			{
				if (const int32* TargetEntryIndex = CageToEntryIndex.Find(ConnectedPattern))
				{
					// Compute actual direction from this cage to the connected cage
					const FVector ConnectedLocation = ConnectedPattern->GetActorLocation();
					const FVector Direction = (ConnectedLocation - CageLocation).GetSafeNormal();

					// Find the correct orbital index based on spatial direction
					// This ensures pattern adjacency matches runtime orbital detection
					// NOTE: We use the orbital set's transform setting, not the cage's,
					// to match how WriteValencyOrbitals computes orbital indices at runtime.
					const uint8 ComputedOrbitalIndex = OrbitalResolver.FindMatchingOrbital(
						Direction,
						bUseTransform,
						CageTransform
					);

					// Find the reciprocal orbital on the target (also compute from direction)
					const FVector ReverseDirection = -Direction;
					const FTransform TargetTransform = ConnectedPattern->GetActorTransform();
					const uint8 ComputedTargetOrbitalIndex = OrbitalResolver.FindMatchingOrbital(
						ReverseDirection,
						bUseTransform,
						TargetTransform
					);

					Entry.Adjacency.Add(FIntVector(*TargetEntryIndex, ComputedOrbitalIndex, ComputedTargetOrbitalIndex));
				}
			}
		}

		PCGEX_VALENCY_VERBOSE(Building, "    Entry[%d] from '%s': %s, %d modules, %d adjacencies, boundary=0x%llX, wildcard=0x%llX",
			EntryIndex, *Cage->GetCageDisplayName(),
			Entry.IsWildcard() ? TEXT("WILDCARD") : (Entry.bIsActive ? TEXT("ACTIVE") : TEXT("CONSTRAINT")),
			Entry.ModuleIndices.Num(),
			Entry.Adjacency.Num(),
			Entry.BoundaryOrbitalMask,
			Entry.WildcardOrbitalMask);
	}

	return OutPattern.Entries.Num() > 0;
}

#undef LOCTEXT_NAMESPACE
