// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "PCGExValencyCommon.h"
#include "PCGExValencyConnectorSet.h"

class UPCGExValencyBondingRules;
class UPCGExValencyOrbitalSet;

/**
 * Description of a module to add to the assembler.
 * At minimum, Asset and OrbitalMask are required.
 */
struct PCGEXELEMENTSVALENCY_API FPCGExAssemblerModuleDesc
{
	/** Required: mesh, actor, or data asset */
	TSoftObjectPtr<UObject> Asset;

	/** Type of asset (for routing to appropriate spawner) */
	EPCGExValencyAssetType AssetType = EPCGExValencyAssetType::Unknown;

	/** Which orbitals this module occupies (bitmask) */
	int64 OrbitalMask = 0;

	/** Module settings (weight, spawn min/max, bounds, dead-end) */
	FPCGExValencyModuleSettings Settings;

	/** Controls how the solver treats this module during placement */
	EPCGExModulePlacementPolicy PlacementPolicy = EPCGExModulePlacementPolicy::Normal;

	/** Optional fixed pick name */
	FName ModuleName;

	/** Optional material variant */
	FPCGExValencyMaterialVariant MaterialVariant;

	/** Whether this module has a material variant (non-default materials) */
	bool bHasMaterialVariant = false;
};

/**
 * Result of an Assembler Validate() or Apply() operation.
 */
struct PCGEXELEMENTSVALENCY_API FPCGExAssemblerResult
{
	bool bSuccess = false;
	int32 ModuleCount = 0;
	TArray<FText> Warnings;
	TArray<FText> Errors;
};

/**
 * Programmatic bonding rules assembler.
 *
 * Populates a UPCGExValencyBondingRules asset purely from data — no cages,
 * no volumes, no editor mode. The Builder delegates to this for the generic
 * module-building logic.
 *
 * Usage:
 *   FPCGExBondingRulesAssembler Assembler;
 *   int32 A = Assembler.AddModule(DescA);
 *   int32 B = Assembler.AddModule(DescB);
 *   Assembler.AddNeighbors(A, "North", {B});
 *   Assembler.AddNeighbors(B, "South", {A});
 *   auto Result = Assembler.Apply(TargetRules);
 */
struct PCGEXELEMENTSVALENCY_API FPCGExBondingRulesAssembler
{
	// === Module Registration ===

	/**
	 * Add a module, returns module index.
	 * Deduplicates by (Asset + OrbitalMask + MaterialVariant).
	 * If a matching module already exists, returns existing index.
	 */
	int32 AddModule(const FPCGExAssemblerModuleDesc& Desc);

	/** Convenience: add by asset + mask only (common case) */
	int32 AddModule(const TSoftObjectPtr<UObject>& Asset, int64 OrbitalMask);

	// === Module Data (additive — call after AddModule) ===

	void AddLocalTransform(int32 ModuleIndex, const FTransform& Transform);
	void AddProperty(int32 ModuleIndex, const FInstancedStruct& Property);
	void AddTag(int32 ModuleIndex, FName Tag);
	void AddConnector(int32 ModuleIndex, const FPCGExValencyModuleConnector& Connector);

	// === Neighbor Relationships ===

	/**
	 * Add valid neighbors for a specific orbital of a module.
	 * Uses OrbitalName (FName) — matches LayerConfig.AddValidNeighbor() convention.
	 */
	void AddNeighbors(int32 ModuleIndex, const FName& OrbitalName, const TArray<int32>& NeighborModuleIndices);

	/**
	 * Mark an orbital as boundary (must have NO neighbor at runtime).
	 * Uses bit index — matches LayerConfig.SetBoundaryOrbital() convention.
	 */
	void SetBoundaryOrbital(int32 ModuleIndex, int32 OrbitalBitIndex);

	/**
	 * Mark an orbital as wildcard (accepts ANY neighbor at runtime).
	 * Uses bit index — matches LayerConfig.SetWildcardOrbital() convention.
	 */
	void SetWildcardOrbital(int32 ModuleIndex, int32 OrbitalBitIndex);

	// === Build ===

	/** Validate internal consistency (property type conflicts, orphan orbitals, etc.) */
	FPCGExAssemblerResult Validate(const UPCGExValencyOrbitalSet* OrbitalSet = nullptr) const;

	/**
	 * Apply assembled modules to target rules + compile.
	 * @param TargetRules The BondingRules asset to populate
	 * @param bClearExisting If true, wipes existing modules before applying
	 * @return Result with module count and any validation messages
	 */
	FPCGExAssemblerResult Apply(
		UPCGExValencyBondingRules* TargetRules,
		bool bClearExisting = true) const;

	// === Query ===

	int32 GetModuleCount() const { return Modules.Num(); }

	int32 FindModule(
		const TSoftObjectPtr<UObject>& Asset,
		int64 OrbitalMask,
		const FPCGExValencyMaterialVariant* MaterialVariant = nullptr) const;

	/** Get the module key-to-index map (for pattern compilation after Apply) */
	const TMap<FString, int32>& GetKeyToIndexMap() const { return KeyToIndex; }

private:
	struct FAssemblerModule
	{
		FPCGExAssemblerModuleDesc Desc;
		TArray<FTransform> LocalTransforms;
		TArray<FInstancedStruct> Properties;
		TArray<FName> Tags;
		TArray<FPCGExValencyModuleConnector> Connectors;

		/** Neighbor data per orbital NAME -> neighbor module indices */
		TMap<FName, TArray<int32>> OrbitalNeighbors;

		/** Boundary/wildcard as bitmasks */
		int64 BoundaryMask = 0;
		int64 WildcardMask = 0;

		/** Dedup key — delegates to PCGExValency::MakeModuleKey() */
		FString MakeKey() const;
	};

	TArray<FAssemblerModule> Modules;
	TMap<FString, int32> KeyToIndex;

	/** Transfer assembled data -> FPCGExValencyModuleDefinition */
	void ApplyModuleToDefinition(
		const FAssemblerModule& Src,
		FPCGExValencyModuleDefinition& Dst) const;

	/** Validate property type consistency */
	bool ValidatePropertyTypes(TArray<FText>& OutErrors) const;
};
