// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "PCGExValencyCommon.h"
#include "PCGExValencyOrbitalSet.h"
#include "PCGExValencyPattern.h"
#include "PCGExProperty.h"

#include "PCGExValencyBondingRules.generated.h"

class UPCGExMeshCollection;
class UPCGExActorCollection;
class UPCGExValencyConnectorSet;

/**
 * Compiled layer data optimized for runtime performance.
 * Uses flattened arrays for cache efficiency (similar to Unity Chemistry pattern).
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExValencyLayerCompiled
{
	GENERATED_BODY()

	/** Layer name */
	UPROPERTY()
	FName LayerName;

	/** Number of orbitals in this layer */
	UPROPERTY()
	int32 OrbitalCount = 0;

	/**
	 * Per-module, per-orbital neighbor data.
	 * Index = ModuleIndex * OrbitalCount + OrbitalIndex
	 * Value.X = Start index in AllNeighbors
	 * Value.Y = Count of valid neighbors
	 */
	UPROPERTY()
	TArray<FIntPoint> NeighborHeaders;

	/** Flattened array of all valid neighbor module indices */
	UPROPERTY()
	TArray<int32> AllNeighbors;

	/** Check if a module's orbital accepts a specific neighbor */
	bool OrbitalAcceptsNeighbor(int32 ModuleIndex, int32 OrbitalIndex, int32 NeighborModuleIndex) const
	{
		const int32 HeaderIndex = ModuleIndex * OrbitalCount + OrbitalIndex;
		if (!NeighborHeaders.IsValidIndex(HeaderIndex))
		{
			return false;
		}

		const FIntPoint& Header = NeighborHeaders[HeaderIndex];
		const int32 Start = Header.X;
		const int32 End = Start + Header.Y;

		for (int32 i = Start; i < End; ++i)
		{
			if (AllNeighbors.IsValidIndex(i) && AllNeighbors[i] == NeighborModuleIndex)
			{
				return true;
			}
		}
		return false;
	}
};

/**
 * Wrapper for module tags array to support UPROPERTY serialization.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExValencyModuleTags
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FName> Tags;

	FPCGExValencyModuleTags() = default;
	explicit FPCGExValencyModuleTags(const TArray<FName>& InTags) : Tags(InTags) {}

	int32 Num() const { return Tags.Num(); }
	const FName& operator[](int32 Index) const { return Tags[Index]; }
};

/**
 * Compiled bonding rules optimized for runtime solving.
 * Serialized with the asset to avoid runtime compilation cost.
 * Can still be rebuilt via Compile() for runtime-created rulesets.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExValencyBondingRulesCompiled
{
	GENERATED_BODY()

	/** Total number of modules */
	UPROPERTY()
	int32 ModuleCount = 0;

	/** Module weights (parallel array) */
	UPROPERTY()
	TArray<float> ModuleWeights;

	/** Module orbital masks (Index = ModuleIndex) */
	UPROPERTY()
	TArray<int64> ModuleOrbitalMasks;

	/** Module boundary orbital masks - orbitals that must have NO neighbor (Index = ModuleIndex) */
	UPROPERTY()
	TArray<int64> ModuleBoundaryMasks;

	/** Module wildcard orbital masks - orbitals that must have ANY neighbor (Index = ModuleIndex) */
	UPROPERTY()
	TArray<int64> ModuleWildcardMasks;

	/** Module min spawn constraints */
	UPROPERTY()
	TArray<int32> ModuleMinSpawns;

	/** Module max spawn constraints (-1 = unlimited) */
	UPROPERTY()
	TArray<int32> ModuleMaxSpawns;

	/** Module asset references */
	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>> ModuleAssets;

	/** Module asset types for routing to appropriate spawner */
	UPROPERTY()
	TArray<EPCGExValencyAssetType> ModuleAssetTypes;

	/** Module names for fixed pick lookup (parallel to ModuleWeights) */
	UPROPERTY()
	TArray<FName> ModuleNames;

	/**
	 * Per-module local transform headers.
	 * X = start index in AllLocalTransforms, Y = count of transforms.
	 * Allows multiple transform variants per module for random selection.
	 */
	UPROPERTY()
	TArray<FIntPoint> ModuleLocalTransformHeaders;

	/** Flattened array of all local transforms for all modules */
	UPROPERTY()
	TArray<FTransform> AllLocalTransforms;

	/** Whether each module has local transform offsets */
	UPROPERTY()
	TArray<bool> ModuleHasLocalTransform;

	/**
	 * Per-module property headers.
	 * X = start index in AllModuleProperties, Y = count of properties.
	 * Allows multiple properties per module with type-safe access.
	 */
	UPROPERTY()
	TArray<FIntPoint> ModulePropertyHeaders;

	/** Flattened array of all properties for all modules */
	UPROPERTY()
	TArray<FInstancedStruct> AllModuleProperties;

	/** Get a random local transform for a module based on seed */
	FTransform GetModuleLocalTransform(int32 ModuleIndex, int32 Seed) const
	{
		if (!ModuleLocalTransformHeaders.IsValidIndex(ModuleIndex))
		{
			return FTransform::Identity;
		}

		const FIntPoint& Header = ModuleLocalTransformHeaders[ModuleIndex];
		if (Header.Y == 0)
		{
			return FTransform::Identity;
		}

		// Use seed to select a transform variant
		const int32 VariantIndex = FMath::Abs(Seed) % Header.Y;
		const int32 TransformIndex = Header.X + VariantIndex;

		if (AllLocalTransforms.IsValidIndex(TransformIndex))
		{
			return AllLocalTransforms[TransformIndex];
		}

		return FTransform::Identity;
	}

	/** Get the number of transform variants for a module */
	int32 GetModuleTransformCount(int32 ModuleIndex) const
	{
		return ModuleLocalTransformHeaders.IsValidIndex(ModuleIndex) ? ModuleLocalTransformHeaders[ModuleIndex].Y : 0;
	}

	/**
	 * Get all properties for a module as a view into the flattened array.
	 * @param ModuleIndex - Index of the module
	 * @return Array view of the module's properties (empty if none)
	 */
	TConstArrayView<FInstancedStruct> GetModuleProperties(int32 ModuleIndex) const
	{
		if (!ModulePropertyHeaders.IsValidIndex(ModuleIndex))
		{
			return TConstArrayView<FInstancedStruct>();
		}

		const FIntPoint& Header = ModulePropertyHeaders[ModuleIndex];
		if (Header.Y == 0 || !AllModuleProperties.IsValidIndex(Header.X))
		{
			return TConstArrayView<FInstancedStruct>();
		}

		return TConstArrayView<FInstancedStruct>(&AllModuleProperties[Header.X], Header.Y);
	}

	/** Get the number of properties for a module */
	int32 GetModulePropertyCount(int32 ModuleIndex) const
	{
		return ModulePropertyHeaders.IsValidIndex(ModuleIndex) ? ModulePropertyHeaders[ModuleIndex].Y : 0;
	}

	/** Per-module actor tags (inherited from cage + palette) */
	UPROPERTY()
	TArray<FPCGExValencyModuleTags> ModuleTags;

	/**
	 * Per-module connector headers.
	 * X = start index in AllModuleConnectors, Y = count of connectors.
	 * Allows efficient connector lookup per module.
	 */
	UPROPERTY()
	TArray<FIntPoint> ModuleConnectorHeaders;

	/** Flattened array of all connectors for all modules (with OrbitalIndex assigned) */
	UPROPERTY()
	TArray<FPCGExValencyModuleConnector> AllModuleConnectors;

	/**
	 * Get all connectors for a module as a view into the flattened array.
	 * @param ModuleIndex - Index of the module
	 * @return Array view of the module's connectors (empty if none)
	 */
	TConstArrayView<FPCGExValencyModuleConnector> GetModuleConnectors(int32 ModuleIndex) const
	{
		if (!ModuleConnectorHeaders.IsValidIndex(ModuleIndex))
		{
			return TConstArrayView<FPCGExValencyModuleConnector>();
		}

		const FIntPoint& Header = ModuleConnectorHeaders[ModuleIndex];
		if (Header.Y == 0 || !AllModuleConnectors.IsValidIndex(Header.X))
		{
			return TConstArrayView<FPCGExValencyModuleConnector>();
		}

		return TConstArrayView<FPCGExValencyModuleConnector>(&AllModuleConnectors[Header.X], Header.Y);
	}

	/** Get the number of connectors for a module */
	int32 GetModuleConnectorCount(int32 ModuleIndex) const
	{
		return ModuleConnectorHeaders.IsValidIndex(ModuleIndex) ? ModuleConnectorHeaders[ModuleIndex].Y : 0;
	}

	/** Per-module placement policy (parallel array). Controls solver participation. */
	UPROPERTY()
	TArray<EPCGExModulePlacementPolicy> ModulePlacementPolicies;

	/** Per-module dead-end flag (parallel array). Dead-end modules terminate growth. */
	UPROPERTY()
	TArray<bool> ModuleIsDeadEnd;

	/** Per-module behavior flags (parallel array). See EPCGExModuleBehavior. */
	UPROPERTY()
	TArray<uint8> ModuleBehaviorFlags;

	/** Whether any module has any behavior flags set (cached at compile time) */
	UPROPERTY()
	bool bHasAnyPreferredStart = false;

	/** Whether any module has PreferredEnd set (cached at compile time) */
	UPROPERTY()
	bool bHasAnyPreferredEnd = false;

	/** Whether any module has Greedy set (cached at compile time) */
	UPROPERTY()
	bool bHasAnyGreedy = false;

	/** Helper: check if a module has a specific behavior flag */
	FORCEINLINE bool ModuleHasBehavior(int32 ModuleIndex, EPCGExModuleBehavior Flag) const
	{
		return (ModuleBehaviorFlags[ModuleIndex] & static_cast<uint8>(Flag)) != 0;
	}

	/** Per-module bounds modifiers for generative solving (parallel array) */
	UPROPERTY()
	TArray<FPCGExBoundsModifier> ModuleBoundsModifiers;

	/** Per-module placement condition headers (X=start, Y=count into AllPlacementConditions) */
	UPROPERTY()
	TArray<FIntPoint> ModulePlacementConditionHeaders;

	/** Flattened placement conditions for all modules */
	UPROPERTY()
	TArray<FInstancedStruct> AllPlacementConditions;

	/** Whether any module has explicit placement conditions (compile-time cache) */
	UPROPERTY()
	bool bHasAnyPlacementConditions = false;

	/** Compiled layer data */
	UPROPERTY()
	FPCGExValencyLayerCompiled Layer;

	/** Compiled patterns for post-solve pattern matching */
	UPROPERTY()
	FPCGExValencyPatternSetCompiled CompiledPatterns;

	/**
	 * Registry of all unique properties found in modules.
	 * Built at compile time for efficient lookup and auto-populate.
	 */
	UPROPERTY()
	TArray<FPCGExPropertyRegistryEntry> ModulePropertyRegistry;

	/**
	 * Registry of all unique properties found in pattern entries.
	 * Built at compile time for efficient lookup.
	 */
	UPROPERTY()
	TArray<FPCGExPropertyRegistryEntry> PatternPropertyRegistry;

	/**
	 * Fast lookup: OrbitalMask -> array of candidate module indices.
	 * Key is the combined mask from all layers (for single-layer, just the mask).
	 * This allows O(1) lookup of which modules could potentially fit a node.
	 * Not serialized - rebuilt on demand via BuildCandidateLookup().
	 */
	TMap<int64, TArray<int32>> MaskToCandidates;

	/** Get a module's orbital mask */
	int64 GetModuleOrbitalMask(int32 ModuleIndex) const
	{
		return ModuleOrbitalMasks.IsValidIndex(ModuleIndex) ? ModuleOrbitalMasks[ModuleIndex] : 0;
	}

	/** Get a module's boundary orbital mask (orbitals that must have no neighbor) */
	int64 GetModuleBoundaryMask(int32 ModuleIndex) const
	{
		return ModuleBoundaryMasks.IsValidIndex(ModuleIndex) ? ModuleBoundaryMasks[ModuleIndex] : 0;
	}

	/** Get a module's wildcard orbital mask (orbitals that must have any neighbor) */
	int64 GetModuleWildcardMask(int32 ModuleIndex) const
	{
		return ModuleWildcardMasks.IsValidIndex(ModuleIndex) ? ModuleWildcardMasks[ModuleIndex] : 0;
	}

	/** Check if a module is excluded from solver placement */
	FORCEINLINE bool IsModuleExcluded(int32 ModuleIndex) const
	{
		return ModulePlacementPolicies.IsValidIndex(ModuleIndex) && ModulePlacementPolicies[ModuleIndex] == EPCGExModulePlacementPolicy::Excluded;
	}

	/** Check if a module is a filler (last-resort placement) */
	FORCEINLINE bool IsModuleFiller(int32 ModuleIndex) const
	{
		return ModulePlacementPolicies.IsValidIndex(ModuleIndex) && ModulePlacementPolicies[ModuleIndex] == EPCGExModulePlacementPolicy::Filler;
	}

	/** Check if a module participates normally in solving */
	FORCEINLINE bool IsModuleNormal(int32 ModuleIndex) const
	{
		return !ModulePlacementPolicies.IsValidIndex(ModuleIndex) || ModulePlacementPolicies[ModuleIndex] == EPCGExModulePlacementPolicy::Normal;
	}

	/** Get the placement conditions for a module */
	TConstArrayView<FInstancedStruct> GetModulePlacementConditions(int32 ModuleIndex) const
	{
		if (!bHasAnyPlacementConditions || !ModulePlacementConditionHeaders.IsValidIndex(ModuleIndex))
		{
			return TConstArrayView<FInstancedStruct>();
		}
		const FIntPoint& Header = ModulePlacementConditionHeaders[ModuleIndex];
		if (Header.Y == 0) { return TConstArrayView<FInstancedStruct>(); }
		return TConstArrayView<FInstancedStruct>(&AllPlacementConditions[Header.X], Header.Y);
	}

	/** Build the MaskToCandidates lookup table */
	void BuildCandidateLookup();
};

/**
 * Main bonding rules data asset - the user-facing configuration.
 * Contains orbital set references and module definitions.
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Valency | Bonding Rules")
class PCGEXELEMENTSVALENCY_API UPCGExValencyBondingRules : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Orbital set defining the orbitals for this bonding ruleset.
	 * Using TObjectPtr ensures the orbital set is loaded when this asset is loaded,
	 * avoiding the need for LoadSynchronous during Compile().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valency|Orbitals")
	TObjectPtr<UPCGExValencyOrbitalSet> OrbitalSet;

	/** Connector set defining connector types and compatibility rules */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valency|Connectors")
	TObjectPtr<UPCGExValencyConnectorSet> ConnectorSet;

	/** Module definitions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valency|Modules")
	TArray<FPCGExValencyModuleDefinition> Modules;

	/**
	 * Generated mesh collection containing all mesh-type modules.
	 * Auto-created during BuildFromCages, never user-edited.
	 * Used by downstream spawners via FPickPacker/FPickUnpacker.
	 */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGExMeshCollection> GeneratedMeshCollection;

	/**
	 * Generated actor collection containing all actor-type modules.
	 * Auto-created during BuildFromCages, never user-edited.
	 * Used by downstream spawners via FPickPacker/FPickUnpacker.
	 */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGExActorCollection> GeneratedActorCollection;

	/**
	 * Mapping from module index to mesh collection entry index.
	 * Only valid entries have their index stored; others are -1.
	 */
	UPROPERTY()
	TArray<int32> ModuleToMeshEntryIndex;

	/**
	 * Mapping from module index to actor collection entry index.
	 * Only valid entries have their index stored; others are -1.
	 */
	UPROPERTY()
	TArray<int32> ModuleToActorEntryIndex;

	/**
	 * Transient: Discovered material variants per mesh asset.
	 * Set by builder during cage scanning, consumed by RebuildGeneratedCollections.
	 * Key = mesh asset soft object path, Value = array of discovered variants.
	 * Not serialized - regenerated on each build.
	 */
	TMap<FSoftObjectPath, TArray<FPCGExValencyMaterialVariant>> DiscoveredMaterialVariants;

	// ========== Patterns ==========

	/**
	 * Compiled patterns for post-solve pattern matching.
	 * Built from pattern cages during BuildFromCages, serialized with the asset.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Valency|Patterns")
	FPCGExValencyPatternSetCompiled Patterns;

	// ========== Default Properties ==========

	/**
	 * Property schema for all modules.
	 * Defines available properties with stable HeaderId identity.
	 * Module-specific values override these defaults.
	 * Synced during compilation - preserves user edits, adds new properties from cages, removes deprecated ones.
	 * ReadOnlySchema metadata prevents users from changing property names/types (synced from cages).
	 */
	UPROPERTY(EditAnywhere, Category = "Valency|Properties", meta=(ReadOnlySchema))
	FPCGExPropertySchemaCollection DefaultProperties;

	/**
	 * Registry of all unique properties (defaults + cage-declared).
	 * Built at compile time for UI display and auto-populate.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Valency|Properties")
	TArray<FPCGExPropertyRegistryEntry> PropertyRegistry;

	// ========== Compiled Data ==========

	/**
	 * If true (default), Compile() is called during BuildFromCages and the result
	 * is serialized with the asset. This avoids runtime compilation cost.
	 * Disable for edge cases where compilation should happen at runtime.
	 */
	UPROPERTY(EditAnywhere, Category = "Valency|Compilation")
	bool bCompileOnRebuild = true;

	/**
	 * Compiled runtime data. Serialized when bCompileOnRebuild is true.
	 * Contains flattened arrays optimized for cache-efficient runtime access.
	 */
	UPROPERTY()
	FPCGExValencyBondingRulesCompiled CompiledData;

	// ========== Build Metadata ==========

	/**
	 * Path of the level where this ruleset was last built.
	 * Used to warn when rebuilding from a different level.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build Info")
	FString LastBuildLevelPath;

	/**
	 * Name of the volume that triggered the last build.
	 * For informational purposes only.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build Info")
	FString LastBuildVolumeName;

	/**
	 * Timestamp of the last build operation.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build Info")
	FDateTime LastBuildTime;

	/** Get module count */
	int32 GetModuleCount() const { return Modules.Num(); }

	/** Get pattern count */
	int32 GetPatternCount() const { return Patterns.GetPatternCount(); }

	/** Check if there are any patterns */
	bool HasPatterns() const { return Patterns.HasPatterns(); }

	/** Get the orbital set */
	const UPCGExValencyOrbitalSet* GetOrbitalSet() const { return OrbitalSet; }

	/** Find a module by asset */
	FPCGExValencyModuleDefinition* FindModuleByAsset(const TSoftObjectPtr<UObject>& Asset)
	{
		for (FPCGExValencyModuleDefinition& Module : Modules)
		{
			if (Module.Asset == Asset)
			{
				return &Module;
			}
		}
		return nullptr;
	}

	/** Get or create a module for an asset */
	FPCGExValencyModuleDefinition& GetOrCreateModule(const TSoftObjectPtr<UObject>& Asset)
	{
		if (FPCGExValencyModuleDefinition* Existing = FindModuleByAsset(Asset))
		{
			return *Existing;
		}

		FPCGExValencyModuleDefinition& NewModule = Modules.AddDefaulted_GetRef();
		NewModule.Asset = Asset;
		return NewModule;
	}

	/**
	 * Compile the bonding rules for runtime use.
	 * Validates orbital sets (already loaded via TObjectPtr), assigns module indices,
	 * and builds optimized lookup structures.
	 * Thread-safe - can be called from any thread.
	 * @return True if compilation succeeded
	 */
	bool Compile();

	/** Check if the bonding rules have valid compiled data */
	bool IsCompiled() const { return CompiledData.ModuleCount > 0; }

	/** Get raw pointer to compiled data for runtime use */
	const FPCGExValencyBondingRulesCompiled* GetCompiledData() const { return CompiledData.ModuleCount > 0 ? &CompiledData : nullptr; }

	/**
	 * Rebuild the generated collections from current modules.
	 * Creates/updates GeneratedMeshCollection and GeneratedActorCollection
	 * based on module asset types. Called automatically by builder.
	 */
	void RebuildGeneratedCollections();

	/** Get the generated mesh collection (may be nullptr if no mesh modules) */
	UPCGExMeshCollection* GetMeshCollection() const { return GeneratedMeshCollection; }

	/** Get the generated actor collection (may be nullptr if no actor modules) */
	UPCGExActorCollection* GetActorCollection() const { return GeneratedActorCollection; }

	/** Get the mesh collection entry index for a module (-1 if not a mesh module) */
	int32 GetMeshEntryIndex(int32 ModuleIndex) const
	{
		return ModuleToMeshEntryIndex.IsValidIndex(ModuleIndex) ? ModuleToMeshEntryIndex[ModuleIndex] : -1;
	}

	/** Get the actor collection entry index for a module (-1 if not an actor module) */
	int32 GetActorEntryIndex(int32 ModuleIndex) const
	{
		return ModuleToActorEntryIndex.IsValidIndex(ModuleIndex) ? ModuleToActorEntryIndex[ModuleIndex] : -1;
	}

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface

	virtual void EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
