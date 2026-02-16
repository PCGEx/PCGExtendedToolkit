// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StructUtils/InstancedStruct.h"
#include "Core/PCGExBondingRulesAssembler.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyConnectorSet.h"

#include "PCGExValencyBondingRulesBuilder.generated.h"

class APCGExValencyCage;
class APCGExValencyCageBase;
class APCGExValencyCagePattern;
class AValencyContextVolume;

/**
 * Result of a build operation.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCYEDITOR_API FPCGExValencyBuildResult
{
	GENERATED_BODY()

	/** Whether the build succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/** Number of modules created/updated */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ModuleCount = 0;

	/** Number of cages processed */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 CageCount = 0;

	/** Number of patterns compiled */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 PatternCount = 0;

	/** Warning messages */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FText> Warnings;

	/** Error messages */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FText> Errors;
};

/**
 * Intermediate cage data used during building.
 */
struct FPCGExValencyCageData
{
	/** The source cage */
	TWeakObjectPtr<APCGExValencyCage> Cage;

	/** Asset entries registered on this cage (with optional local transforms) */
	TArray<FPCGExValencyAssetEntry> AssetEntries;

	/** Orbital mask (which orbitals are connected) */
	int64 OrbitalMask = 0;

	/** Module settings from the cage */
	FPCGExValencyModuleSettings Settings;

	/** Placement policy from the cage */
	EPCGExModulePlacementPolicy PlacementPolicy = EPCGExModulePlacementPolicy::Normal;

	/** Module name from the cage (for fixed picks) */
	FName ModuleName;

	/** Whether this cage preserves local transforms */
	bool bPreserveLocalTransforms = false;

	/** Properties from cage property components */
	TArray<FInstancedStruct> Properties;

	/** Actor tags from cage and mirrored sources (palettes, other cages) */
	TArray<FName> Tags;

	/** Per-orbital: module indices of valid neighbors (from connected cage's assets) */
	TMap<int32, TArray<int32>> OrbitalToNeighborModules;

	/** Connector definitions collected from cage connector components and/or auto-extracted from assets */
	TArray<FPCGExValencyModuleConnector> Connectors;

	/** Whether this cage has auto-extraction enabled */
	bool bReadConnectorsFromAssets = false;

	/** Delegates to PCGExValency::MakeModuleKey (LocalTransform param kept for API compat but ignored). */
	static FString MakeModuleKey(const FSoftObjectPath& AssetPath, int64 Mask, const FTransform* LocalTransform = nullptr, const FPCGExValencyMaterialVariant* MaterialVariant = nullptr)
	{
		return PCGExValency::MakeModuleKey(AssetPath, Mask, MaterialVariant);
	}
};

/**
 * Builds BondingRules from cages placed in a level.
 *
 * Usage:
 * 1. Create builder instance
 * 2. Call BuildFromVolume() with target volume
 * 3. Check result for success/warnings/errors
 */
UCLASS(BlueprintType)
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExValencyBondingRulesBuilder : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Build bonding rules from all cages within a volume.
	 * Updates the volume's BondingRules asset in place.
	 *
	 * @param Volume The volume containing cages
	 * @return Build result with success status and any warnings/errors
	 */
	UFUNCTION(BlueprintCallable, Category = "Valency")
	FPCGExValencyBuildResult BuildFromVolume(AValencyContextVolume* Volume);

	/**
	 * Build bonding rules from all cages within multiple volumes.
	 * All volumes must reference the same BondingRules asset.
	 * Updates the shared BondingRules asset in place.
	 *
	 * @param Volumes Array of volumes containing cages (must share same BondingRules)
	 * @param TriggeringVolume The volume that initiated the build (for metadata)
	 * @return Build result with success status and any warnings/errors
	 */
	UFUNCTION(BlueprintCallable, Category = "Valency")
	FPCGExValencyBuildResult BuildFromVolumes(const TArray<AValencyContextVolume*>& Volumes, AValencyContextVolume* TriggeringVolume = nullptr);

	/**
	 * Build bonding rules from a specific set of cages.
	 *
	 * @param Cages Array of cages to process
	 * @param TargetRules The BondingRules asset to update
	 * @param OrbitalSet The orbital set to use for direction matching
	 * @return Build result with success status and any warnings/errors
	 */
	UFUNCTION(BlueprintCallable, Category = "Valency")
	FPCGExValencyBuildResult BuildFromCages(
		const TArray<APCGExValencyCage*>& Cages,
		UPCGExValencyBondingRules* TargetRules,
		UPCGExValencyOrbitalSet* OrbitalSet
	);

	/** If true, clears existing modules before building (default: true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build Options")
	bool bClearExistingModules = true;

	/** If true, validates completeness after building */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build Options")
	bool bValidateCompleteness = true;

	/** If true, includes null cage connections as valid boundaries */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build Options")
	bool bIncludeNullBoundaries = true;

protected:
	/**
	 * Collect and preprocess cage data.
	 * Resolves mirror sources, computes orbital masks, etc.
	 */
	void CollectCageData(
		const TArray<APCGExValencyCage*>& Cages,
		const UPCGExValencyOrbitalSet* OrbitalSet,
		TArray<FPCGExValencyCageData>& OutCageData,
		FPCGExValencyBuildResult& OutResult
	);

	/**
	 * Populate an Assembler from collected cage data.
	 * Replaces BuildModuleMap + BuildNeighborRelationships + validation.
	 * Pass 1: Register modules from asset entries.
	 * Pass 2: Set neighbor relationships from orbital connections.
	 *
	 * @param CageData Collected cage data from CollectCageData
	 * @param OrbitalSet The orbital set for name/index lookup
	 * @param OutAssembler The assembler to populate
	 */
	void PopulateAssembler(
		const TArray<FPCGExValencyCageData>& CageData,
		const UPCGExValencyOrbitalSet* OrbitalSet,
		FPCGExBondingRulesAssembler& OutAssembler
	);

	/**
	 * Get effective asset entries for a cage (resolving mirror sources).
	 */
	TArray<FPCGExValencyAssetEntry> GetEffectiveAssetEntries(const APCGExValencyCage* Cage);

	/**
	 * Collect properties from a cage and its mirror sources.
	 * Properties from palettes and mirrored cages are inherited.
	 */
	TArray<FInstancedStruct> GetEffectiveProperties(const APCGExValencyCage* Cage);

	/**
	 * Collect actor tags from a cage and its mirror sources.
	 * Tags from palettes and mirrored cages are inherited.
	 */
	TArray<FName> GetEffectiveTags(const APCGExValencyCage* Cage);

	/**
	 * Generate a variant name for a module based on its asset and configuration.
	 */
	static FString GenerateVariantName(const FPCGExValencyAssetEntry& Entry, int64 OrbitalMask, bool bHasLocalTransform);

	/**
	 * Collect material variants from all cages.
	 * Cages discover variants during scanning; this merges them into TargetRules.
	 * Stores results in TargetRules->DiscoveredMaterialVariants.
	 */
	void DiscoverMaterialVariants(
		const TArray<FPCGExValencyCageData>& CageData,
		UPCGExValencyBondingRules* TargetRules
	);

	/**
	 * Compile patterns from pattern cages.
	 * Pattern cages are discovered within the volumes, grouped by connectivity,
	 * and compiled into FPCGExValencyPatternSetCompiled.
	 *
	 * @param Volumes Array of volumes to search for pattern cages
	 * @param ModuleKeyToIndex Mapping from module key to compiled module index
	 * @param TargetRules The BondingRules asset to update with compiled patterns
	 * @param OrbitalSet The orbital set to use for orbital index lookup
	 * @param OutResult Build result for warnings/errors
	 */
	void CompilePatterns(
		const TArray<AValencyContextVolume*>& Volumes,
		const TMap<FString, int32>& ModuleKeyToIndex,
		UPCGExValencyBondingRules* TargetRules,
		const UPCGExValencyOrbitalSet* OrbitalSet,
		FPCGExValencyBuildResult& OutResult
	);

	/**
	 * Compile a single pattern from its root cage.
	 * Traverses connected pattern cages and builds the compiled pattern.
	 *
	 * @param RootCage The pattern root cage
	 * @param ModuleKeyToIndex Mapping from module key to compiled module index
	 * @param TargetRules The BondingRules for module lookup
	 * @param OrbitalSet The orbital set for orbital index lookup
	 * @param OutPattern The compiled pattern
	 * @param OutResult Build result for warnings/errors
	 * @return true if compilation succeeded
	 */
	bool CompileSinglePattern(
		APCGExValencyCagePattern* RootCage,
		const TMap<FString, int32>& ModuleKeyToIndex,
		UPCGExValencyBondingRules* TargetRules,
		const UPCGExValencyOrbitalSet* OrbitalSet,
		FPCGExValencyPatternCompiled& OutPattern,
		FPCGExValencyBuildResult& OutResult
	);
};
