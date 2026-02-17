// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExValencyBondingRules.h"

#include "Core/PCGExConnectorTransformStrategy.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExActorCollection.h"
#include "Engine/Blueprint.h"
#include "Core/PCGExValencyLog.h"

void FPCGExValencyBondingRulesCompiled::BuildCandidateLookup()
{
	MaskToCandidates.Empty();

	for (int32 ModuleIndex = 0; ModuleIndex < ModuleCount; ++ModuleIndex)
	{
		if (IsModuleExcluded(ModuleIndex)) { continue; }

		const int64 Mask = ModuleOrbitalMasks[ModuleIndex];
		TArray<int32>& Candidates = MaskToCandidates.FindOrAdd(Mask);
		Candidates.Add(ModuleIndex);
	}
}

bool UPCGExValencyBondingRules::Compile()
{
	VALENCY_LOG_SECTION(Compilation, "BONDING RULES COMPILATION START");
	PCGEX_VALENCY_INFO(Compilation, "Asset: %s", *GetName());
	PCGEX_VALENCY_INFO(Compilation, "Module count: %d, OrbitalSet: %s", Modules.Num(), OrbitalSet ? *OrbitalSet->LayerName.ToString() : TEXT("null"));

	// Validate orbital set (TObjectPtr ensures it's already loaded with this asset)
	if (!OrbitalSet)
	{
		PCGEX_VALENCY_ERROR(Compilation, "OrbitalSet is null");
		return false;
	}

	PCGEX_VALENCY_INFO(Compilation, "  OrbitalSet: '%s' with %d orbitals", *OrbitalSet->LayerName.ToString(), OrbitalSet->Num());

	// Validate orbital set
	{
		TArray<FText> ValidationErrors;
		if (!OrbitalSet->Validate(ValidationErrors))
		{
			for (const FText& Error : ValidationErrors)
			{
				PCGEX_VALENCY_ERROR(Compilation, "%s", *Error.ToString());
			}
			return false;
		}
	}

	// Check for more than 64 orbitals
	if (OrbitalSet->Num() > 64)
	{
		PCGEX_VALENCY_ERROR(Compilation, "OrbitalSet '%s' has more than 64 orbitals", *OrbitalSet->LayerName.ToString());
		return false;
	}

	// Reset compiled data for fresh compilation
	CompiledData = FPCGExValencyBondingRulesCompiled();

	CompiledData.ModuleCount = Modules.Num();

	// Initialize parallel arrays
	CompiledData.ModuleWeights.SetNum(Modules.Num());
	CompiledData.ModuleMinSpawns.SetNum(Modules.Num());
	CompiledData.ModuleMaxSpawns.SetNum(Modules.Num());
	CompiledData.ModuleAssets.SetNum(Modules.Num());
	CompiledData.ModuleAssetTypes.SetNum(Modules.Num());
	CompiledData.ModuleNames.SetNum(Modules.Num());
	CompiledData.ModuleLocalTransformHeaders.SetNum(Modules.Num());
	CompiledData.ModuleHasLocalTransform.SetNum(Modules.Num());
	CompiledData.ModuleOrbitalMasks.SetNum(Modules.Num());
	CompiledData.ModuleBoundaryMasks.SetNum(Modules.Num());
	CompiledData.ModuleWildcardMasks.SetNum(Modules.Num());
	CompiledData.AllLocalTransforms.Empty();
	CompiledData.ModulePropertyHeaders.SetNum(Modules.Num());
	CompiledData.AllModuleProperties.Empty();
	CompiledData.ModuleTags.SetNum(Modules.Num());
	CompiledData.ModuleConnectorHeaders.SetNum(Modules.Num());
	CompiledData.AllModuleConnectors.Empty();
	CompiledData.ModulePlacementPolicies.SetNum(Modules.Num());
	CompiledData.ModuleIsDeadEnd.SetNum(Modules.Num());
	CompiledData.ModuleBehaviorFlags.SetNum(Modules.Num());
	CompiledData.bHasAnyPreferredStart = false;
	CompiledData.bHasAnyPreferredEnd = false;
	CompiledData.bHasAnyGreedy = false;
	CompiledData.ModuleBoundsModifiers.SetNum(Modules.Num());
	CompiledData.ModulePlacementConditionHeaders.SetNum(Modules.Num());
	CompiledData.AllPlacementConditions.Empty();
	CompiledData.bHasAnyPlacementConditions = false;

	// Populate module data
	VALENCY_LOG_SUBSECTION(Compilation, "Compiling Module Data");
	for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
	{
		const FPCGExValencyModuleDefinition& Module = Modules[ModuleIndex];

		CompiledData.ModuleWeights[ModuleIndex] = Module.Settings.Weight;
		CompiledData.ModuleMinSpawns[ModuleIndex] = Module.Settings.MinSpawns;
		CompiledData.ModuleMaxSpawns[ModuleIndex] = Module.Settings.MaxSpawns;
		CompiledData.ModuleAssets[ModuleIndex] = Module.Asset;
		CompiledData.ModuleAssetTypes[ModuleIndex] = Module.AssetType;
		CompiledData.ModuleNames[ModuleIndex] = Module.ModuleName;
		CompiledData.ModuleHasLocalTransform[ModuleIndex] = Module.bHasLocalTransform;
		CompiledData.ModulePlacementPolicies[ModuleIndex] = Module.PlacementPolicy;
		CompiledData.ModuleIsDeadEnd[ModuleIndex] = Module.Settings.bIsDeadEnd;
		CompiledData.ModuleBehaviorFlags[ModuleIndex] = Module.Settings.BehaviorFlags;
		if (Module.Settings.HasBehavior(EPCGExModuleBehavior::PreferredStart)) { CompiledData.bHasAnyPreferredStart = true; }
		if (Module.Settings.HasBehavior(EPCGExModuleBehavior::PreferredEnd)) { CompiledData.bHasAnyPreferredEnd = true; }
		if (Module.Settings.HasBehavior(EPCGExModuleBehavior::Greedy)) { CompiledData.bHasAnyGreedy = true; }
		CompiledData.ModuleBoundsModifiers[ModuleIndex] = Module.Settings.BoundsModifier;

		// Populate placement condition header and flattened conditions
		const int32 ConditionStartIndex = CompiledData.AllPlacementConditions.Num();
		const int32 ConditionCount = Module.Settings.PlacementConditions.Num();
		CompiledData.ModulePlacementConditionHeaders[ModuleIndex] = FIntPoint(ConditionStartIndex, ConditionCount);
		if (ConditionCount > 0)
		{
			CompiledData.AllPlacementConditions.Append(Module.Settings.PlacementConditions);
			CompiledData.bHasAnyPlacementConditions = true;
		}

		// Populate transform header and flattened transforms
		const int32 TransformStartIndex = CompiledData.AllLocalTransforms.Num();
		const int32 TransformCount = Module.LocalTransforms.Num();
		CompiledData.ModuleLocalTransformHeaders[ModuleIndex] = FIntPoint(TransformStartIndex, TransformCount);
		CompiledData.AllLocalTransforms.Append(Module.LocalTransforms);

		// Populate property header and flattened properties
		const int32 PropertyStartIndex = CompiledData.AllModuleProperties.Num();
		const int32 PropertyCount = Module.Properties.Num();
		CompiledData.ModulePropertyHeaders[ModuleIndex] = FIntPoint(PropertyStartIndex, PropertyCount);
		CompiledData.AllModuleProperties.Append(Module.Properties);

		// Copy module tags
		CompiledData.ModuleTags[ModuleIndex] = FPCGExValencyModuleTags(Module.Tags);

		// Populate connector header and flattened connectors with orbital index assignment
		const int32 ConnectorStartIndex = CompiledData.AllModuleConnectors.Num();
		int32 ConnectorCount = 0;

		if (Module.Connectors.Num() > 0 && OrbitalSet)
		{
			// Build orbital direction resolver
			PCGExValency::FOrbitalDirectionResolver OrbitalResolver;
			OrbitalResolver.BuildFrom(OrbitalSet);

			for (const FPCGExValencyModuleConnector& Connector : Module.Connectors)
			{
				FPCGExValencyModuleConnector CompiledConnector = Connector;

				// Apply connector transform strategy (if set)
				if (const FPCGExConnectorTransformStrategy* Strategy =
					Module.ConnectorTransformStrategy.GetPtr<FPCGExConnectorTransformStrategy>())
				{
					Strategy->TransformConnector(CompiledConnector.LocalOffset, Module.AssetRelativeTransform);
				}

				if (Connector.bManualOrbitalOverride)
				{
					// Manual override - use user-specified orbital index
					CompiledConnector.OrbitalIndex = FMath::Clamp(Connector.ManualOrbitalIndex, 0, 63);
				}
				else
				{
					// Compute orbital index from connector direction (now mesh-relative after adjustment)
					const FVector ConnectorDirection = CompiledConnector.LocalOffset.GetTranslation().GetSafeNormal();

					if (!ConnectorDirection.IsNearlyZero())
					{
						// Use the orbital resolver to find matching orbital
						// Note: For connectors, we typically don't transform by module rotation (connectors are module-local)
						CompiledConnector.OrbitalIndex = OrbitalResolver.FindMatchingOrbital(
							ConnectorDirection,
							false, // Don't transform - connectors are in module-local space
							FTransform::Identity
						);
					}
					else
					{
						// Connector at origin - assign to orbital 0 as fallback, or -1 for invalid
						CompiledConnector.OrbitalIndex = OrbitalSet->Num() > 0 ? 0 : -1;
					}
				}

				CompiledData.AllModuleConnectors.Add(CompiledConnector);
				ConnectorCount++;

				PCGEX_VALENCY_VERBOSE(Compilation, "    Connector '%s' (type=%s, polarity=%s) -> OrbitalIndex=%d",
					*Connector.Identifier.ToString(),
					*Connector.ConnectorType.ToString(),
					Connector.Polarity == EPCGExConnectorPolarity::Universal ? TEXT("Universal") : Connector.Polarity == EPCGExConnectorPolarity::Plug ? TEXT("Plug") : TEXT("Port"),
					CompiledConnector.OrbitalIndex);
			}
		}

		CompiledData.ModuleConnectorHeaders[ModuleIndex] = FIntPoint(ConnectorStartIndex, ConnectorCount);

		PCGEX_VALENCY_VERBOSE(Compilation, "  Module[%d]: Asset='%s', Weight=%.2f, Type=%d, Properties=%d, Tags=%d, Connectors=%d",
			ModuleIndex,
			*Module.Asset.GetAssetName(),
			Module.Settings.Weight,
			static_cast<int32>(Module.AssetType),
			PropertyCount,
			Module.Tags.Num(),
			ConnectorCount);

		// Orbital masks
		{
			const FPCGExValencyModuleLayerConfig& Config = Module.LayerConfig;
			CompiledData.ModuleOrbitalMasks[ModuleIndex] = Config.OrbitalMask;
			CompiledData.ModuleBoundaryMasks[ModuleIndex] = Config.BoundaryOrbitalMask;
			CompiledData.ModuleWildcardMasks[ModuleIndex] = Config.WildcardOrbitalMask;

			// Log orbital mask as binary for easier reading
			FString OrbitalBits;
			FString BoundaryBits;
			FString WildcardBits;
			for (int32 Bit = 0; Bit < OrbitalSet->Num(); ++Bit)
			{
				OrbitalBits += (Config.OrbitalMask & (1LL << Bit)) ? TEXT("1") : TEXT("0");
				BoundaryBits += (Config.BoundaryOrbitalMask & (1LL << Bit)) ? TEXT("1") : TEXT("0");
				WildcardBits += (Config.WildcardOrbitalMask & (1LL << Bit)) ? TEXT("1") : TEXT("0");
			}
			PCGEX_VALENCY_VERBOSE(Compilation, "    OrbitalMask=%s, BoundaryMask=%s, WildcardMask=%s",
				*OrbitalBits, *BoundaryBits, *WildcardBits);

			// Log neighbor info
			for (const auto& NeighborPair : Config.OrbitalNeighbors)
			{
				PCGEX_VALENCY_VERBOSE(Compilation, "      Orbital '%s' neighbors: [%s]",
					*NeighborPair.Key.ToString(),
					*FString::JoinBy(NeighborPair.Value.Indices, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));
			}
		}
	}

	// Compile layer neighbor data
	{
		FPCGExValencyLayerCompiled& CompiledLayer = CompiledData.Layer;

		CompiledLayer.LayerName = OrbitalSet->LayerName;
		CompiledLayer.OrbitalCount = OrbitalSet->Num();

		// Prepare header array (ModuleCount * OrbitalCount entries)
		const int32 HeaderCount = Modules.Num() * OrbitalSet->Num();
		CompiledLayer.NeighborHeaders.SetNum(HeaderCount);
		CompiledLayer.AllNeighbors.Empty();

		// Flatten neighbor data
		for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
		{
			const FPCGExValencyModuleDefinition& Module = Modules[ModuleIndex];
			const FPCGExValencyModuleLayerConfig& Config = Module.LayerConfig;

			for (int32 OrbitalIndex = 0; OrbitalIndex < OrbitalSet->Num(); ++OrbitalIndex)
			{
				const int32 HeaderIndex = ModuleIndex * OrbitalSet->Num() + OrbitalIndex;
				const FName OrbitalName = OrbitalSet->Orbitals[OrbitalIndex].GetOrbitalName();

				int32 NeighborStart = CompiledLayer.AllNeighbors.Num();
				int32 NeighborCount = 0;

				if (const FPCGExValencyNeighborIndices* Neighbors = Config.OrbitalNeighbors.Find(OrbitalName))
				{
					NeighborCount = Neighbors->Num();
					CompiledLayer.AllNeighbors.Append(Neighbors->Indices);
				}

				CompiledLayer.NeighborHeaders[HeaderIndex] = FIntPoint(NeighborStart, NeighborCount);
			}
		}
	}

	// Build fast lookup
	CompiledData.BuildCandidateLookup();

	// Sync DefaultProperties schema to match cage-declared properties
	VALENCY_LOG_SUBSECTION(Compilation, "Syncing Default Properties Schema");
	{
		// Collect all unique properties from cages (by HeaderId for stable identity)
		TMap<int32, FPCGExPropertySchema> CageDeclaredSchemas;

		#if WITH_EDITOR
		for (const FInstancedStruct& PropStruct : CompiledData.AllModuleProperties)
		{
			if (const FPCGExProperty* Prop = PropStruct.GetPtr<FPCGExProperty>())
			{
				if (!Prop->PropertyName.IsNone() && Prop->HeaderId != 0)
				{
					if (!CageDeclaredSchemas.Contains(Prop->HeaderId))
					{
						// Create schema from first occurrence
						FPCGExPropertySchema NewSchema;
						NewSchema.HeaderId = Prop->HeaderId;
						NewSchema.Name = Prop->PropertyName;
						NewSchema.Property = PropStruct;
						CageDeclaredSchemas.Add(Prop->HeaderId, MoveTemp(NewSchema));
					}
				}
			}
		}
		#endif

		// Build map of existing schemas by HeaderId (to preserve user edits)
		TMap<int32, FPCGExPropertySchema> ExistingSchemasByHeaderId;
		#if WITH_EDITOR
		for (FPCGExPropertySchema& Schema : DefaultProperties.Schemas)
		{
			if (Schema.HeaderId != 0)
			{
				ExistingSchemasByHeaderId.Add(Schema.HeaderId, MoveTemp(Schema));
			}
		}
		#endif

		// Rebuild schema: keep existing (if still used), add new, remove deprecated
		DefaultProperties.Schemas.Reset();

		#if WITH_EDITOR
		for (auto& CageSchemaPair : CageDeclaredSchemas)
		{
			const int32 HeaderId = CageSchemaPair.Key;
			FPCGExPropertySchema& CageSchema = CageSchemaPair.Value;

			if (FPCGExPropertySchema* ExistingSchema = ExistingSchemasByHeaderId.Find(HeaderId))
			{
				// Keep existing schema (preserves user-edited default values)
				// Update name from cage (handles renames)
				ExistingSchema->Name = CageSchema.Name;

				// Check if type changed
				if (ExistingSchema->Property.GetScriptStruct() == CageSchema.Property.GetScriptStruct())
				{
					// Same type - preserve value, update PropertyName
					if (FPCGExProperty* Prop = ExistingSchema->GetPropertyMutable())
					{
						Prop->PropertyName = CageSchema.Name;
					}
					DefaultProperties.Schemas.Add(MoveTemp(*ExistingSchema));
					PCGEX_VALENCY_VERBOSE(Compilation, "  Preserved schema for property '%s'", *CageSchema.Name.ToString());
				}
				else
				{
					// Type changed - use cage's default and warn
					DefaultProperties.Schemas.Add(MoveTemp(CageSchema));
					PCGEX_VALENCY_WARNING(Compilation, "Property '%s' type changed, reset to cage default", *CageSchema.Name.ToString());
				}
			}
			else
			{
				// New property - add cage schema
				DefaultProperties.Schemas.Add(MoveTemp(CageSchema));
				PCGEX_VALENCY_INFO(Compilation, "  Added new property schema '%s'", *CageSchema.Name.ToString());
			}
		}

		// Log removed schemas
		for (const auto& ExistingPair : ExistingSchemasByHeaderId)
		{
			if (!CageDeclaredSchemas.Contains(ExistingPair.Key))
			{
				PCGEX_VALENCY_INFO(Compilation, "  Removed deprecated property schema (HeaderId=%d)", ExistingPair.Key);
			}
		}
		#endif

		PCGEX_VALENCY_INFO(Compilation, "DefaultProperties schema synced: %d properties", DefaultProperties.Num());
	}

	// Build module property registry (includes defaults + cage properties)
	VALENCY_LOG_SUBSECTION(Compilation, "Building Property Registries");
	{
		TMap<FName, FPCGExPropertyRegistryEntry> ModulePropertiesMap;

		// Add defaults from schema
		for (const FPCGExPropertySchema& Schema : DefaultProperties.Schemas)
		{
			if (const FPCGExProperty* Prop = Schema.GetProperty())
			{
				if (!Schema.Name.IsNone())
				{
					ModulePropertiesMap.Add(Schema.Name, Prop->ToRegistryEntry());
				}
			}
		}

		// Scan all module properties
		for (const FInstancedStruct& PropStruct : CompiledData.AllModuleProperties)
		{
			if (const FPCGExProperty* Prop = PropStruct.GetPtr<FPCGExProperty>())
			{
				if (!Prop->PropertyName.IsNone() && !ModulePropertiesMap.Contains(Prop->PropertyName))
				{
					ModulePropertiesMap.Add(Prop->PropertyName, Prop->ToRegistryEntry());
				}
			}
		}

		// Convert to array and sort by name for consistent ordering
		ModulePropertiesMap.GenerateValueArray(CompiledData.ModulePropertyRegistry);
		CompiledData.ModulePropertyRegistry.Sort([](const FPCGExPropertyRegistryEntry& A, const FPCGExPropertyRegistryEntry& B)
		{
			return A.PropertyName.LexicalLess(B.PropertyName);
		});

		// Copy registry to bonding rules for UI display
		PropertyRegistry = CompiledData.ModulePropertyRegistry;

		PCGEX_VALENCY_INFO(Compilation, "Module property registry: %d unique properties", CompiledData.ModulePropertyRegistry.Num());
		for (const FPCGExPropertyRegistryEntry& Entry : CompiledData.ModulePropertyRegistry)
		{
			PCGEX_VALENCY_VERBOSE(Compilation, "  - %s (%s, output=%s)",
				*Entry.PropertyName.ToString(),
				*Entry.TypeName.ToString(),
				Entry.bSupportsOutput ? TEXT("yes") : TEXT("no"));
		}
	}

	// Build pattern property registry
	{
		TMap<FName, FPCGExPropertyRegistryEntry> PatternPropertiesMap;

		// Scan all pattern entry properties
		for (const FPCGExValencyPatternCompiled& Pattern : Patterns.Patterns)
		{
			for (const FPCGExValencyPatternEntryCompiled& Entry : Pattern.Entries)
			{
				for (const FInstancedStruct& PropStruct : Entry.Properties)
				{
					if (const FPCGExProperty* Prop = PropStruct.GetPtr<FPCGExProperty>())
					{
						if (!Prop->PropertyName.IsNone() && !PatternPropertiesMap.Contains(Prop->PropertyName))
						{
							PatternPropertiesMap.Add(Prop->PropertyName, Prop->ToRegistryEntry());
						}
					}
				}
			}
		}

		// Convert to array and sort
		PatternPropertiesMap.GenerateValueArray(CompiledData.PatternPropertyRegistry);
		CompiledData.PatternPropertyRegistry.Sort([](const FPCGExPropertyRegistryEntry& A, const FPCGExPropertyRegistryEntry& B)
		{
			return A.PropertyName.LexicalLess(B.PropertyName);
		});

		PCGEX_VALENCY_INFO(Compilation, "Pattern property registry: %d unique properties", CompiledData.PatternPropertyRegistry.Num());
		for (const FPCGExPropertyRegistryEntry& Entry : CompiledData.PatternPropertyRegistry)
		{
			PCGEX_VALENCY_VERBOSE(Compilation, "  - %s (%s, output=%s)",
				*Entry.PropertyName.ToString(),
				*Entry.TypeName.ToString(),
				Entry.bSupportsOutput ? TEXT("yes") : TEXT("no"));
		}
	}

	// Copy patterns (already compiled by builder, stored on this asset)
	CompiledData.CompiledPatterns = Patterns;
	PCGEX_VALENCY_INFO(Compilation, "Patterns: %d total (%d exclusive, %d additive)",
		Patterns.GetPatternCount(),
		Patterns.ExclusivePatternIndices.Num(),
		Patterns.AdditivePatternIndices.Num());

	VALENCY_LOG_SECTION(Compilation, "BONDING RULES COMPILATION COMPLETE");
	PCGEX_VALENCY_INFO(Compilation, "Result: %d modules, %d patterns", Modules.Num(), Patterns.GetPatternCount());
	return true;
}

void UPCGExValencyBondingRules::PostLoad()
{
	Super::PostLoad();

	// If already compiled (serialized data), just rebuild the non-serialized lookup table
	if (IsCompiled())
	{
		CompiledData.BuildCandidateLookup();
		return;
	}

	// Otherwise compile if we have the required data
	if (Modules.Num() > 0 && OrbitalSet)
	{
		Compile();
	}
}

void UPCGExValencyBondingRules::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
#if WITH_EDITOR
	Context->EDITOR_TrackPath(this);
	if (GeneratedMeshCollection) { GeneratedMeshCollection->EDITOR_RegisterTrackingKeys(Context); }
	if (GeneratedActorCollection) { GeneratedActorCollection->EDITOR_RegisterTrackingKeys(Context); }
#endif
}

void UPCGExValencyBondingRules::RebuildGeneratedCollections()
{
	// Initialize module-to-entry mappings
	ModuleToMeshEntryIndex.SetNum(Modules.Num());
	ModuleToActorEntryIndex.SetNum(Modules.Num());
	for (int32 i = 0; i < Modules.Num(); ++i)
	{
		ModuleToMeshEntryIndex[i] = -1;
		ModuleToActorEntryIndex[i] = -1;
	}

	// Count modules by type
	int32 MeshCount = 0;
	int32 ActorCount = 0;

	for (const FPCGExValencyModuleDefinition& Module : Modules)
	{
		if (Module.AssetType == EPCGExValencyAssetType::Mesh)
		{
			MeshCount++;
		}
		else if (Module.AssetType == EPCGExValencyAssetType::Actor)
		{
			ActorCount++;
		}
	}

	// Create or clear mesh collection
	if (MeshCount > 0)
	{
		if (!GeneratedMeshCollection)
		{
			const FName CollectionName = *FString::Printf(TEXT("%s_Meshes"), *GetName());
			GeneratedMeshCollection = NewObject<UPCGExMeshCollection>(this, CollectionName, RF_Transactional);
		}
		GeneratedMeshCollection->Entries.Empty();
		GeneratedMeshCollection->Entries.Reserve(MeshCount);

		int32 EntryIndex = 0;
		for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
		{
			const FPCGExValencyModuleDefinition& Module = Modules[ModuleIndex];
			if (Module.AssetType == EPCGExValencyAssetType::Mesh)
			{
				FPCGExMeshCollectionEntry& Entry = GeneratedMeshCollection->Entries.AddDefaulted_GetRef();
				Entry.StaticMesh = TSoftObjectPtr<UStaticMesh>(Module.Asset.ToSoftObjectPath());
				Entry.Weight = FMath::Max(1, FMath::RoundToInt32(Module.Settings.Weight));

				// Populate material variant if this module has one
				// Each module now has its own material variant stored directly, not looked up by mesh path
				if (Module.bHasMaterialVariant && Module.MaterialVariant.Overrides.Num() > 0)
				{
					const FPCGExValencyMaterialVariant& Variant = Module.MaterialVariant;

					// Determine mode based on number of overrides
					if (Variant.Overrides.Num() == 1)
					{
						// Single slot mode
						Entry.MaterialVariants = EPCGExMaterialVariantsMode::Single;
						Entry.SlotIndex = Variant.Overrides[0].SlotIndex;
						Entry.MaterialOverrideVariants.Empty();

						FPCGExMaterialOverrideSingleEntry& SingleEntry = Entry.MaterialOverrideVariants.AddDefaulted_GetRef();
						SingleEntry.Weight = Variant.DiscoveryCount;
						SingleEntry.Material = Variant.Overrides[0].Material;
					}
					else
					{
						// Multi slot mode
						Entry.MaterialVariants = EPCGExMaterialVariantsMode::Multi;
						Entry.MaterialOverrideVariantsList.Empty();

						FPCGExMaterialOverrideCollection& MultiEntry = Entry.MaterialOverrideVariantsList.AddDefaulted_GetRef();
						MultiEntry.Weight = Variant.DiscoveryCount;
						MultiEntry.Overrides.Reserve(Variant.Overrides.Num());

						for (const FPCGExValencyMaterialOverride& Override : Variant.Overrides)
						{
							FPCGExMaterialOverrideEntry& OverrideEntry = MultiEntry.Overrides.AddDefaulted_GetRef();
							OverrideEntry.SlotIndex = Override.SlotIndex;
							OverrideEntry.Material = Override.Material;
						}
					}
				}

				// Store mapping
				ModuleToMeshEntryIndex[ModuleIndex] = EntryIndex;
				++EntryIndex;
			}
		}

		// Rebuild internal indices
		GeneratedMeshCollection->RebuildStagingData(true);
	}
	else
	{
		GeneratedMeshCollection = nullptr;
	}

	// Create or clear actor collection
	if (ActorCount > 0)
	{
		if (!GeneratedActorCollection)
		{
			const FName CollectionName = *FString::Printf(TEXT("%s_Actors"), *GetName());
			GeneratedActorCollection = NewObject<UPCGExActorCollection>(this, CollectionName, RF_Transactional);
		}
		GeneratedActorCollection->Entries.Empty();
		GeneratedActorCollection->Entries.Reserve(ActorCount);

		int32 EntryIndex = 0;
		for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
		{
			const FPCGExValencyModuleDefinition& Module = Modules[ModuleIndex];
			if (Module.AssetType == EPCGExValencyAssetType::Actor)
			{
				// Actor modules store a Blueprint, we need to get the GeneratedClass from it
				if (UObject* LoadedAsset = Module.Asset.LoadSynchronous())
				{
					if (const UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset))
					{
						if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
						{
							FPCGExActorCollectionEntry& Entry = GeneratedActorCollection->Entries.AddDefaulted_GetRef();
							Entry.Actor = TSoftClassPtr<AActor>(Blueprint->GeneratedClass.Get());
							Entry.Weight = FMath::Max(1, FMath::RoundToInt32(Module.Settings.Weight));

							// Store mapping
							ModuleToActorEntryIndex[ModuleIndex] = EntryIndex;
							++EntryIndex;
						}
					}
				}
			}
		}

		// Rebuild internal indices if we have entries
		if (GeneratedActorCollection->Entries.Num() > 0)
		{
			GeneratedActorCollection->RebuildStagingData(true);
		}
		else
		{
			GeneratedActorCollection = nullptr;
		}
	}
	else
	{
		GeneratedActorCollection = nullptr;
	}

	// Mark as dirty for save
	Modify();
}

#if WITH_EDITOR
void UPCGExValencyBondingRules::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Auto-compile when properties change in editor
	// This could be made optional via a flag if compilation is expensive
	// Compile();
}
#endif
