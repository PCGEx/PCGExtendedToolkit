// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyEditorActorBase.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExConnectorTransformStrategy.h"

#include "PCGExValencyAssetContainerBase.generated.h"

class UStaticMeshComponent;
class APCGExValencyCage;

/**
 * Flags controlling which local transform components are preserved when registering assets.
 * Used when bPreserveLocalTransforms is enabled.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPCGExLocalTransformFlags : uint8
{
	None = 0 UMETA(Hidden),
	/** Preserve local translation (position relative to cage center) */
	Translation = 1 << 0,
	/** Preserve local rotation (orientation relative to cage) */
	Rotation = 1 << 1,
	/** Preserve local scale */
	Scale = 1 << 2,

	// Common combinations
	All = Translation | Rotation | Scale UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EPCGExLocalTransformFlags);

/**
 * Abstract base for Valency actors that manage asset collections (cages and palettes).
 * Provides shared asset storage, scanning helpers, material variant tracking,
 * local transform preservation, and mirroring cage resolution.
 *
 * Subclasses must implement ScanAndRegisterContainedAssets() for their specific
 * actor iteration and containment logic.
 */
UCLASS(Abstract, HideCategories = (Rendering, Replication, Collision, HLOD, Physics, Networking, Input, LOD, Cooking))
class PCGEXELEMENTSVALENCYEDITOR_API APCGExValencyAssetContainerBase : public APCGExValencyEditorActorBase
{
	GENERATED_BODY()

public:
	APCGExValencyAssetContainerBase();

	//~ Begin Containment Interface

	/**
	 * Check if an actor is inside this container's detection bounds.
	 * Override in subclasses to implement custom containment logic.
	 * @param Actor The actor to test
	 * @return True if the actor is considered "inside" this container
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Container|Detection")
	bool IsActorInside(AActor* Actor) const;
	virtual bool IsActorInside_Implementation(AActor* Actor) const { return false; }

	/**
	 * Check if a world location is inside this container's detection bounds.
	 * Override in subclasses to implement custom containment logic.
	 * @param WorldLocation The location to test
	 * @return True if the location is inside this container
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Container|Detection")
	bool ContainsPoint(const FVector& WorldLocation) const;
	virtual bool ContainsPoint_Implementation(const FVector& WorldLocation) const { return false; }

	//~ End Containment Interface

	/** Get all asset entries (combines manual + scanned, dedupes, stamps ModuleSettings) */
	virtual TArray<FPCGExValencyAssetEntry> GetAllAssetEntries() const;

	/** Get manual asset entries only */
	const TArray<FPCGExValencyAssetEntry>& GetManualAssetEntries() const { return ManualAssetEntries; }

	/** Get scanned asset entries only */
	const TArray<FPCGExValencyAssetEntry>& GetScannedAssetEntries() const { return ScannedAssetEntries; }

	/** Get discovered material variants */
	const TMap<FSoftObjectPath, TArray<FPCGExValencyMaterialVariant>>& GetDiscoveredMaterialVariants() const
	{
		return DiscoveredMaterialVariants;
	}

	/** Check if a specific local transform component should be preserved */
	bool ShouldPreserveTranslation() const
	{
		return bPreserveLocalTransforms && EnumHasAnyFlags(static_cast<EPCGExLocalTransformFlags>(LocalTransformFlags), EPCGExLocalTransformFlags::Translation);
	}

	bool ShouldPreserveRotation() const
	{
		return bPreserveLocalTransforms && EnumHasAnyFlags(static_cast<EPCGExLocalTransformFlags>(LocalTransformFlags), EPCGExLocalTransformFlags::Rotation);
	}

	bool ShouldPreserveScale() const
	{
		return bPreserveLocalTransforms && EnumHasAnyFlags(static_cast<EPCGExLocalTransformFlags>(LocalTransformFlags), EPCGExLocalTransformFlags::Scale);
	}

	/** Whether the connector transform strategy depends on asset-to-cage relative transforms */
	bool IsConnectorTransformSensitive() const;

	/** Compute the local transform to preserve based on flags */
	FTransform ComputePreservedLocalTransform(const FTransform& AssetWorldTransform) const;

	/**
	 * Find all cages that have this actor in their MirrorSources array.
	 * Used to cascade updates when this actor's content changes.
	 */
	void FindMirroringCages(TArray<APCGExValencyCage*>& OutCages) const;

	/**
	 * Check if scanned assets have changed compared to a previous snapshot.
	 * @param OldScannedAssets Previous scanned assets to compare against
	 * @return True if assets have changed
	 */
	bool HaveScannedAssetsChanged(const TArray<FPCGExValencyAssetEntry>& OldScannedAssets) const;

	/** Clear scanned assets (auto-detected) */
	void ClearScannedAssets();

	//~ Begin APCGExValencyEditorActorBase Interface
	virtual void CollectDraggableActors(TArray<AActor*>& OutActors) const override;
	//~ End APCGExValencyEditorActorBase Interface

public:
	/**
	 * Manually registered asset entries (user-defined via details panel).
	 * These are persisted and not affected by auto-scanning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets", meta = (TitleProperty = "Asset", PCGEX_ValencyGhostRefresh))
	TArray<FPCGExValencyAssetEntry> ManualAssetEntries;

	/**
	 * Auto-scanned asset entries (transient, rebuilt by ScanAndRegisterContainedAssets).
	 * Populated when bAutoRegisterContainedAssets is enabled.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Assets")
	TArray<FPCGExValencyAssetEntry> ScannedAssetEntries;

	/**
	 * Whether to automatically scan for and register contained assets.
	 * If false, assets must be manually registered.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta=(PCGEX_ValencyRebuild))
	bool bAutoRegisterContainedAssets = true;

	/**
	 * When enabled, preserves the spatial relationship between assets and the container center.
	 * Useful when asset placement within the container matters (e.g., corner placement).
	 * Each unique Asset + LocalTransform combination becomes a separate module variant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta=(PCGEX_ValencyRebuild, InlineEditConditionToggle))
	bool bPreserveLocalTransforms = false;

	/**
	 * Which components of the local transform to preserve.
	 * Only used when bPreserveLocalTransforms is enabled.
	 * Default: Rotation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (Bitmask, BitmaskEnum = "/Script/PCGExElementsValencyEditor.EPCGExLocalTransformFlags", EditCondition = "bPreserveLocalTransforms", PCGEX_ValencyRebuild))
	uint8 LocalTransformFlags = static_cast<uint8>(EPCGExLocalTransformFlags::Rotation);

	/**
	 * Module settings applied to all assets in this container.
	 * These settings are copied to module definitions when building rules.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
	FPCGExValencyModuleSettings ModuleSettings;

	/**
	 * Strategy for transforming connector offsets during compilation.
	 * Determines how connectors are adjusted from cage-space to the target coordinate space.
	 * Default: Asset Relative (adjusts connectors to mesh-pivot-space).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module",
		meta=(BaseStruct="/Script/PCGExElementsValency.PCGExConnectorTransformStrategy",
			ExcludeBaseStruct, PCGEX_ValencyRebuild))
	FInstancedStruct ConnectorTransformStrategy;

protected:
	/**
	 * Add a scanned entry with duplicate checking and material variant support.
	 * Extracted from the duplicated lambda in ScanAndRegisterContainedAssets.
	 * @param Asset The asset to add
	 * @param SourceActor The actor the asset was scanned from
	 * @param InMaterialVariant Optional material variant (nullptr for default materials)
	 */
	void AddScannedEntry(const TSoftObjectPtr<UObject>& Asset, AActor* SourceActor, const FPCGExValencyMaterialVariant* InMaterialVariant);

	/** Extract material overrides from a static mesh component */
	static void ExtractMaterialOverrides(const UStaticMeshComponent* MeshComponent, TArray<FPCGExValencyMaterialOverride>& OutOverrides);

	/** Record a material variant for a mesh asset */
	void RecordMaterialVariant(const FSoftObjectPath& MeshPath, const TArray<FPCGExValencyMaterialOverride>& Overrides);

	/** Called when asset registration changes. Base: Modify() + redraw viewport */
	virtual void OnAssetRegistrationChanged();

	/**
	 * Material variants discovered during asset scanning.
	 * Key = mesh asset path, Value = array of unique material configurations.
	 * Populated by ScanAndRegisterContainedAssets, consumed by builder.
	 */
	TMap<FSoftObjectPath, TArray<FPCGExValencyMaterialVariant>> DiscoveredMaterialVariants;
};
