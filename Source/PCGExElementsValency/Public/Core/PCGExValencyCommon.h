// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExH.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "StructUtils/InstancedStruct.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExConnectorTransformStrategy.h"
#include "Core/PCGExPlacementCondition.h"

#include "PCGExValencyCommon.generated.h"

/**
 * Type of asset referenced by a Valency module.
 * Used to route to appropriate spawner (mesh vs actor vs data asset).
 */
UENUM(BlueprintType)
enum class EPCGExValencyAssetType : uint8
{
	/** Type not yet determined or unknown */
	Unknown = 0,
	/** Static mesh asset (UStaticMesh) */
	Mesh,
	/** Actor class or Blueprint (spawned as actor) */
	Actor,
	/** PCG Data Asset (UPCGDataAsset) */
	DataAsset
};

/**
 * Mode for placeholder (null) cages - determines runtime constraint behavior.
 *
 * BITMASK INVARIANTS (see Orbital_Bitmask_Reference.md):
 * - Boundary: BoundaryMask SET, OrbitalMask NOT set (must have NO neighbor)
 * - Wildcard: WildcardMask SET, OrbitalMask SET (must have ANY neighbor)
 * - Any: Neither mask set, OrbitalMask NOT set (no constraint)
 * - INVARIANT: Boundary & Wildcard == 0 (mutually exclusive)
 */
UENUM(BlueprintType)
enum class EPCGExPlaceholderMode : uint8
{
	Boundary UMETA(ToolTip = "Orbital must have NO neighbor at runtime"),
	Wildcard UMETA(ToolTip = "Orbital must have ANY neighbor at runtime"),
	Any UMETA(ToolTip = "No runtime constraint - spatial placeholder only")
};

/**
 * Default behavior for cage orbitals without explicit connections.
 * Applied during BuildNeighborRelationships when an orbital has no connected cage.
 */
UENUM(BlueprintType)
enum class EPCGExMissingConnectionBehavior : uint8
{
	Unconstrained UMETA(ToolTip = "No constraint - any neighbor or none accepted"),
	Boundary UMETA(ToolTip = "Treat as boundary - must have NO neighbor"),
	Wildcard UMETA(ToolTip = "Treat as wildcard - must have ANY neighbor")
};

/**
 * Controls how the solver treats modules derived from this cage.
 */
UENUM(BlueprintType)
enum class EPCGExModulePlacementPolicy : uint8
{
	Normal UMETA(ToolTip = "Standard solver participation"),
	Filler UMETA(ToolTip = "Last-resort placement, no constraint propagation"),
	Excluded UMETA(ToolTip = "Not placed - sockets and metadata only")
};

namespace PCGExValency
{
	/** Algorithm state constants */
	namespace SlotState
	{
		constexpr int32 UNSET = -1;        // Not yet resolved
		constexpr int32 NULL_SLOT = -2;    // Boundary / no neighbor exists
		constexpr int32 UNSOLVABLE = -3;   // Contradiction detected
		constexpr int32 PLACEHOLDER = -4;  // For ligature replacement
	}

	/** Pin labels */
	namespace Labels
	{
		const FName SourceBondingRulesLabel = FName("BondingRules");
		const FName SourceSolverLabel = FName("Solver");
		const FName SourceClustersLabel = FName("Clusters");
		const FName SourceFixedPickFiltersLabel = FName("FixedPickFilters");

		const FName SourceValencyMapLabel = FName("Valency Map");
		const FName OutputValencyMapLabel = FName("Valency Map");
		const FName Tag_ValencyRulesPath = FName("PCGEx/V/Rules/Path");
		const FName Tag_ValencyMapIdx = FName("PCGEx/V/Rules/Idx");
		const FName Tag_OrbitalCount = FName("PCGEx/V/Rules/OrbitalCount");
	}

	/** Suffix-based attribute name helpers for orbital data. */
	namespace Attributes
	{
		static FName GetMaskAttributeName(const FName& Suffix)
		{
			return FName(FString::Printf(TEXT("PCGEx/V/Mask/%s"), *Suffix.ToString()));
		}

		static FName GetOrbitalAttributeName(const FName& Suffix)
		{
			return FName(FString::Printf(TEXT("PCGEx/V/Orbital/%s"), *Suffix.ToString()));
		}
	}

	/**
	 * Pack/unpack helpers for the PCGEx/V/Orbital/{Id} edge attribute.
	 * Layout: byte[0] = start node's orbital index, byte[1] = end node's orbital index.
	 * Max 255 orbital indices (uint8); 0xFF = no match (sentinel).
	 *
	 * NOTE: WriteOrbitals uses byte-level writes (PackedBytes[ByteOffset]) for thread-safe
	 * per-node processing. That pattern can't call Pack() but shares the same binary layout.
	 */
	namespace EdgeOrbital
	{
		/** Sentinel for unmatched orbital endpoint. Same as PCGExValency::NO_ORBITAL_MATCH. */
		constexpr uint8 NO_MATCH = 0xFF;

		/** Pack start/end orbital indices into int64 for edge attribute storage. */
		FORCEINLINE int64 Pack(const uint8 StartOrbital, const uint8 EndOrbital)
		{
			return static_cast<int64>(
				static_cast<uint64>(StartOrbital) |
				(static_cast<uint64>(EndOrbital) << 8));
		}

		/** Extract start node's orbital index from packed edge data. */
		FORCEINLINE uint8 GetStartOrbital(const int64 Packed)
		{
			return static_cast<uint8>(Packed & 0xFF);
		}

		/** Extract end node's orbital index from packed edge data. */
		FORCEINLINE uint8 GetEndOrbital(const int64 Packed)
		{
			return static_cast<uint8>((Packed >> 8) & 0xFF);
		}

		/** Initial sentinel value for edges (both endpoints = no match). */
		constexpr int64 NoMatchSentinel()
		{
			return static_cast<int64>(
				static_cast<uint64>(NO_MATCH) |
				(static_cast<uint64>(NO_MATCH) << 8));
		}
	}

	/**
	 * Pack/unpack helpers for the PCGEx/V/Connector/{Id} edge attribute.
	 * Encodes which connector INSTANCE sits at each edge endpoint (by flat index into
	 * AllModuleConnectors). Uses H64: upper 32 bits = source, lower 32 bits = target.
	 *
	 * NOT the same as PCGExValencyConnector::Pack() which encodes connector TYPE identity
	 * (RulesIndex + ConnectorTypeIndex in 16-bit fields).
	 */
	namespace EdgeConnector
	{
		/** Pack source/target connector instance indices into int64. */
		FORCEINLINE int64 Pack(const int32 SourceIndex, const int32 TargetIndex)
		{
			return static_cast<int64>(PCGEx::H64(SourceIndex, TargetIndex));
		}

		/** Extract source (start) connector instance index. */
		FORCEINLINE int32 GetSourceIndex(const int64 Packed)
		{
			return static_cast<int32>(PCGEx::H64A(static_cast<uint64>(Packed)));
		}

		/** Extract target (end) connector instance index. */
		FORCEINLINE int32 GetTargetIndex(const int64 Packed)
		{
			return static_cast<int32>(PCGEx::H64B(static_cast<uint64>(Packed)));
		}
	}

	/**
	 * Minimal per-node state for valency processing.
	 * Contains node identity and solver output only.
	 * Orbital data is accessed via FOrbitalCache.
	 */
	struct PCGEXELEMENTSVALENCY_API FValencyState
	{
		/** Index in the cluster (node-space, not point-space) */
		int32 NodeIndex = -1;

		/** Output: resolved module index, or special SlotState value */
		int32 ResolvedModule = SlotState::UNSET;

		/** Check if this state has been resolved (success, boundary, or unsolvable) */
		FORCEINLINE bool IsResolved() const { return ResolvedModule >= 0 || ResolvedModule == SlotState::NULL_SLOT || ResolvedModule == SlotState::UNSOLVABLE; }

		/** Check if this is a boundary state (no orbitals, marked NULL) */
		FORCEINLINE bool IsBoundary() const { return ResolvedModule == SlotState::NULL_SLOT; }

		/** Check if this state failed to solve (contradiction) */
		FORCEINLINE bool IsUnsolvable() const { return ResolvedModule == SlotState::UNSOLVABLE; }
	};
}


/**
 * Scale/offset modifier applied to a module's staging bounds for overlap checking.
 * Used by the generative solver to tune collision volumes independently of visual bounds.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExBoundsModifier
{
	GENERATED_BODY()

	/** Scale applied to the asset's staging bounds for overlap checking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	FVector Scale = FVector::OneVector;

	/** Offset added to bounds center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	FVector Offset = FVector::ZeroVector;

	/** Apply this modifier to a source FBox */
	FBox Apply(const FBox& InBounds) const
	{
		const FVector Center = InBounds.GetCenter() + Offset;
		const FVector Extent = InBounds.GetExtent() * Scale;
		return FBox(Center - Extent, Center + Extent);
	}

	bool IsDefault() const
	{
		return Scale.Equals(FVector::OneVector) && Offset.IsNearlyZero();
	}
};

/**
 * Shared module settings - used on cages and in module definitions.
 * Cages are the source of truth; BondingRules are compiled from cages.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyModuleSettings
{
	GENERATED_BODY()

	/** Probability weight for selection (higher = more likely) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0.001", PCGEX_ValencyRebuild))
	float Weight = 1.0f;

	/** Minimum number of times this module must be placed (0 = no minimum) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", PCGEX_ValencyRebuild))
	int32 MinSpawns = 0;

	/** Maximum number of times this module can be placed (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "-1", PCGEX_ValencyRebuild))
	int32 MaxSpawns = -1;

	/** Bounds modifier for overlap checking in generative solving */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Generative", meta = (PCGEX_ValencyRebuild))
	FPCGExBoundsModifier BoundsModifier;

	/** If true, this module terminates growth - its sockets are not expanded */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Generative", meta = (PCGEX_ValencyRebuild))
	bool bIsDeadEnd = false;

	/** If true, this module is preferred as a growth starting point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Generative", meta = (PCGEX_ValencyRebuild))
	bool bPreferredStartingPoint = false;

	/** Placement conditions that must ALL pass for module placement during growth.
	 *  Stack is evaluated in order — first failure rejects placement.
	 *  Empty = no validation (module places unconditionally). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Generative",
		meta=(BaseStruct="/Script/PCGExElementsValency.PCGExPlacementCondition",
			  ExcludeBaseStruct, PCGEX_ValencyRebuild))
	TArray<FInstancedStruct> PlacementConditions;
};

/**
 * A single material override entry (slot index + material).
 * Used during material variant discovery.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyMaterialOverride
{
	GENERATED_BODY()

	/** Material slot index */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	int32 SlotIndex = 0;

	/** The override material */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	TSoftObjectPtr<UMaterialInterface> Material;

	bool operator==(const FPCGExValencyMaterialOverride& Other) const
	{
		return SlotIndex == Other.SlotIndex && Material == Other.Material;
	}
};

/**
 * A discovered material variant configuration.
 * Represents a unique material configuration seen on a mesh during cage scanning.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyMaterialVariant
{
	GENERATED_BODY()

	/** Material overrides for this variant (slot → material) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	TArray<FPCGExValencyMaterialOverride> Overrides;

	/** Discovery count - how many times this configuration was seen (becomes weight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	int32 DiscoveryCount = 1;

	bool operator==(const FPCGExValencyMaterialVariant& Other) const
	{
		if (Overrides.Num() != Other.Overrides.Num()) { return false; }
		for (int32 i = 0; i < Overrides.Num(); ++i)
		{
			if (!(Overrides[i] == Other.Overrides[i])) { return false; }
		}
		return true;
	}
};

namespace PCGExValency
{
	/**
	 * Module identity key — used for deduplication during building.
	 * Same key = same module (Asset + OrbitalMask + optional MaterialVariant).
	 * LocalTransform is NOT part of identity (transform variants share the same module).
	 */
	inline FString MakeModuleKey(
		const FSoftObjectPath& AssetPath,
		int64 OrbitalMask,
		const FPCGExValencyMaterialVariant* MaterialVariant = nullptr)
	{
		FString Key = FString::Printf(TEXT("%s_%lld"), *AssetPath.ToString(), OrbitalMask);

		// Include material variant hash - each unique material configuration is a separate module
		if (MaterialVariant && MaterialVariant->Overrides.Num() > 0)
		{
			Key += TEXT("_M");
			for (const FPCGExValencyMaterialOverride& Override : MaterialVariant->Overrides)
			{
				// Use material path hash for uniqueness
				const uint32 MatHash = Override.Material.IsValid()
					? GetTypeHash(Override.Material.ToSoftObjectPath().ToString())
					: 0;
				Key += FString::Printf(TEXT("%d:%u"), Override.SlotIndex, MatHash);
			}
		}

		return Key;
	}
}

/**
 * An asset entry within a cage, with optional local transform.
 * When bPreserveLocalTransform is enabled on the cage, the LocalTransform
 * represents the asset's position relative to the cage center.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyAssetEntry
{
	GENERATED_BODY()

	/** The asset (mesh, blueprint, actor class, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TSoftObjectPtr<UObject> Asset;

	/** Detected type of the asset (for routing to appropriate spawner) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset", meta=(PCGEX_ValencyRebuild))
	EPCGExValencyAssetType AssetType = EPCGExValencyAssetType::Unknown;

	/** Transform relative to cage center (used when cage has bPreserveLocalTransforms enabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	FTransform LocalTransform = FTransform::Identity;

	/** Source actor this entry was scanned from (transient, not saved) */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SourceActor;

	/**
	 * Material variant for this specific entry instance.
	 * Stores the material overrides seen on this particular asset placement.
	 * Populated during cage scanning when materials differ from mesh defaults.
	 */
	UPROPERTY(Transient)
	FPCGExValencyMaterialVariant MaterialVariant;

	/** Whether this entry has a non-default material configuration */
	UPROPERTY(Transient)
	bool bHasMaterialVariant = false;

	/**
	 * Whether this entry's LocalTransform should be preserved in module building.
	 * Set when the source cage/palette has bPreserveLocalTransforms enabled.
	 * Carried through mirroring so palette transforms are respected.
	 */
	UPROPERTY(Transient)
	bool bPreserveLocalTransform = false;

	/**
	 * Module settings (weight, spawn constraints) for this entry.
	 * Populated from the source cage/palette's ModuleSettings during collection.
	 * When mirroring, carries the SOURCE's settings (not the primary cage's).
	 */
	UPROPERTY(Transient)
	FPCGExValencyModuleSettings Settings;

	/** Whether this entry has custom settings (vs using defaults) */
	UPROPERTY(Transient)
	bool bHasSettings = false;

	bool IsValid() const { return !Asset.IsNull(); }
};

/**
 * Wrapper for array of module indices (needed for TMap UPROPERTY support).
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyNeighborIndices
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbors")
	TArray<int32> Indices;

	void Add(int32 Index) { Indices.Add(Index); }
	void AddUnique(int32 Index) { Indices.AddUnique(Index); }
	int32 Num() const { return Indices.Num(); }
	bool Contains(int32 Index) const { return Indices.Contains(Index); }
};

/**
 * Per-layer orbital configuration for a module.
 * Stores which orbitals this module has and which neighbors are valid for each orbital.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyModuleLayerConfig
{
	GENERATED_BODY()

	/** Bitmask indicating which orbitals this module has (bits set = orbital exists) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int64 OrbitalMask = 0;

	/** Bitmask indicating which orbitals connect to boundaries/null cages (no neighbors expected) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int64 BoundaryOrbitalMask = 0;

	/** Bitmask indicating which orbitals connect to wildcards (any neighbor required) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int64 WildcardOrbitalMask = 0;

	/** Valid neighbor module indices per orbital. Key = OrbitalName, Value = array of valid module indices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	TMap<FName, FPCGExValencyNeighborIndices> OrbitalNeighbors;

	/** Check if this module has a specific orbital */
	bool HasOrbital(int32 BitIndex) const
	{
		return (OrbitalMask & (1LL << BitIndex)) != 0;
	}

	/** Check if an orbital connects to a boundary (null cage) */
	bool IsBoundaryOrbital(int32 BitIndex) const
	{
		return (BoundaryOrbitalMask & (1LL << BitIndex)) != 0;
	}

	/** Check if an orbital connects to a wildcard (any neighbor required) */
	bool IsWildcardOrbital(int32 BitIndex) const
	{
		return (WildcardOrbitalMask & (1LL << BitIndex)) != 0;
	}

	/** Set an orbital as present */
	void SetOrbital(int32 BitIndex)
	{
		OrbitalMask |= (1LL << BitIndex);
	}

	/** Mark an orbital as connecting to a boundary */
	void SetBoundaryOrbital(int32 BitIndex)
	{
		BoundaryOrbitalMask |= (1LL << BitIndex);
	}

	/** Mark an orbital as connecting to a wildcard (any neighbor required) */
	void SetWildcardOrbital(int32 BitIndex)
	{
		WildcardOrbitalMask |= (1LL << BitIndex);
		OrbitalMask |= (1LL << BitIndex); // Orbital EXISTS and requires a neighbor
	}

	/** Add a valid neighbor for an orbital */
	void AddValidNeighbor(const FName& OrbitalName, int32 NeighborModuleIndex)
	{
		FPCGExValencyNeighborIndices& Neighbors = OrbitalNeighbors.FindOrAdd(OrbitalName);
		Neighbors.AddUnique(NeighborModuleIndex);
	}
};

/**
 * A module definition - represents one placeable asset with its orbital configuration.
 * Modules are uniquely identified by Asset + OrbitalMask + LocalTransform combination.
 * Same asset with different connectivity or placement = different modules.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyModuleDefinition
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/**
	 * Display name for this module variant (auto-generated).
	 * Helps identify modules during review. E.g., "Cube_NE_4conn"
	 */
	FString VariantName;
#endif

	/** The asset to spawn (mesh, actor, data asset, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	TSoftObjectPtr<UObject> Asset;

	/** Type of asset (for routing to appropriate spawner) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Module")
	EPCGExValencyAssetType AssetType = EPCGExValencyAssetType::Unknown;

	/**
	 * Local transforms relative to spawn point (variants).
	 * Used when the source cage had bPreserveLocalTransforms enabled.
	 * Multiple transforms allow different rotations/positions of the same asset
	 * to be randomly selected at output time based on point seed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	TArray<FTransform> LocalTransforms;

	/** Whether this module uses local transform offsets */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Module")
	bool bHasLocalTransform = false;

	/** Add a local transform variant (skips near-duplicates) */
	void AddLocalTransform(const FTransform& Transform)
	{
		// Skip near-duplicate transforms
		for (const FTransform& Existing : LocalTransforms)
		{
			if (Existing.Equals(Transform, 0.1f))
			{
				return;
			}
		}

		LocalTransforms.Add(Transform);

		// Has local transform if we have any non-identity transforms
		if (!bHasLocalTransform && !Transform.Equals(FTransform::Identity, 0.01f))
		{
			bHasLocalTransform = true;
		}
	}

	/**
	 * Material variant for this specific module.
	 * Stored directly on the module so each module has its own unique material configuration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	FPCGExValencyMaterialVariant MaterialVariant;

	/** Whether this module has a material variant (non-default materials) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Module")
	bool bHasMaterialVariant = false;

	/** Module settings (weight, spawn constraints) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	FPCGExValencyModuleSettings Settings;

	/** How the solver treats this module during placement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	EPCGExModulePlacementPolicy PlacementPolicy = EPCGExModulePlacementPolicy::Normal;

	/**
	 * Optional name for this module (from source cage).
	 * Used for fixed picks - allows pre-assigning specific modules to vertices.
	 * Empty = no name (cannot be fixed-picked by name).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	FName ModuleName;

	/** Orbital configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	FPCGExValencyModuleLayerConfig LayerConfig;

	/**
	 * Properties from cage property components.
	 * Accessible at solver, matcher, and replacement stages.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	TArray<FInstancedStruct> Properties;

	/**
	 * Actor tags from source cage and mirrored palettes.
	 * Useful for filtering during solving, pattern matching, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	TArray<FName> Tags;

	/**
	 * Connector definitions for this module.
	 * Connectors are typed connection points that map to orbital indices.
	 * Used for connector-based orbital assignment (alternative to direction-based).
	 * Populated from cage connector components during building.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module|Connectors")
	TArray<FPCGExValencyModuleConnector> Connectors;

	/** Full asset-to-cage relative transform.
	 *  Applied to connectors during compilation so they become mesh-pivot-relative.
	 *  Identity when asset origin matches cage origin (no adjustment needed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Module")
	FTransform AssetRelativeTransform = FTransform::Identity;

	/** Strategy for transforming connector offsets during compilation.
	 *  Determines how connectors are adjusted from cage-space to the target coordinate space.
	 *  Empty = no transform (cage-relative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module|Connectors",
		meta=(BaseStruct="/Script/PCGExElementsValency.PCGExConnectorTransformStrategy", ExcludeBaseStruct))
	FInstancedStruct ConnectorTransformStrategy;

	/** Check if this module can still be spawned given current spawn count */
	bool CanSpawn(int32 CurrentSpawnCount) const
	{
		return Settings.MaxSpawns < 0 || CurrentSpawnCount < Settings.MaxSpawns;
	}

	/** Check if this module needs more spawns to meet minimum */
	bool NeedsMoreSpawns(int32 CurrentSpawnCount) const
	{
		return CurrentSpawnCount < Settings.MinSpawns;
	}

	/** Get a unique key for this module (Asset path + orbital mask) */
	FString GetModuleKey() const
	{
		return FString::Printf(TEXT("%s_%lld"), *Asset.ToSoftObjectPath().ToString(), LayerConfig.OrbitalMask);
	}

	/** Check if this module has any connectors defined */
	bool HasConnectors() const { return Connectors.Num() > 0; }

	/** Check if this module has any plug connectors (for chaining) */
	bool HasPlugConnectors() const
	{
		for (const FPCGExValencyModuleConnector& Connector : Connectors)
		{
			if (Connector.Polarity == EPCGExConnectorPolarity::Plug) { return true; }
		}
		return false;
	}

	/** Find a connector by identifier */
	const FPCGExValencyModuleConnector* FindConnectorByIdentifier(const FName& Identifier) const
	{
		for (const FPCGExValencyModuleConnector& Connector : Connectors)
		{
			if (Connector.Identifier == Identifier) { return &Connector; }
		}
		return nullptr;
	}

	/** Find a connector by type (returns first match) */
	const FPCGExValencyModuleConnector* FindConnectorByType(const FName& ConnectorType) const
	{
		for (const FPCGExValencyModuleConnector& Connector : Connectors)
		{
			if (Connector.ConnectorType == ConnectorType) { return &Connector; }
		}
		return nullptr;
	}

	/** Get all plug connectors */
	TArray<const FPCGExValencyModuleConnector*> GetPlugConnectors() const
	{
		TArray<const FPCGExValencyModuleConnector*> Result;
		for (const FPCGExValencyModuleConnector& Connector : Connectors)
		{
			if (Connector.Polarity == EPCGExConnectorPolarity::Plug) { Result.Add(&Connector); }
		}
		return Result;
	}

	/** Get all port connectors */
	TArray<const FPCGExValencyModuleConnector*> GetPortConnectors() const
	{
		TArray<const FPCGExValencyModuleConnector*> Result;
		for (const FPCGExValencyModuleConnector& Connector : Connectors)
		{
			if (Connector.Polarity == EPCGExConnectorPolarity::Port) { Result.Add(&Connector); }
		}
		return Result;
	}
};
