// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGManagedResource.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExPreloadResources.generated.h"

class UPCGExAssetCollection;

UENUM()
enum class EPCGExPreloadResourcesMode : uint8
{
	Preload UMETA(DisplayName = "Preload Resources", ToolTip = "Load external assets and keep them resident across graph executions."),
	Release UMETA(DisplayName = "Release Resources", ToolTip = "Drop every resource this node-type previously preloaded on the running component, freeing them for GC / cache eviction."),
};

/** Which categories of a collection's referenced assets to preload. Orthogonal flags, OR-combined. */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPCGExCollectionLoadFlags : uint8
{
	None             = 0 UMETA(Hidden),
	DirectAssets     = 1 << 0 UMETA(DisplayName = "Direct Assets", ToolTip = "Assets referenced by the collection's own top-level entries."),
	SubCollections   = 1 << 1 UMETA(DisplayName = "Sub-Collections", ToolTip = "Recurse into nested sub-collections."),
	CustomProperties = 1 << 2 UMETA(DisplayName = "Custom Properties", ToolTip = "Soft object/class paths carried by the collection's custom properties (and per-entry overrides)."),
};

ENUM_CLASS_FLAGS(EPCGExCollectionLoadFlags)

/**
 * PCG managed resource that keeps a batch of preloaded assets resident across graph executions.
 *
 * Two independent holds, deliberately belt-and-suspenders:
 *  - Handles: streamable keep-alive wrappers (also warm in the per-world subsystem cache).
 *  - RootedAssets: hard UObject refs that GC-root the resolved assets.
 * Either alone keeps the assets loaded; both together survives changes to either subsystem.
 *
 * Lifetime mirrors PCGEx managed resources generally: PCG marks all resources unused each
 * (re)generation; the Preload node re-marks the matching one by CRC (so identical configs are
 * reused with no reload), and PCG Release()s the rest -- so stale path sets drop automatically.
 */
UCLASS()
class UPCGExManagedPreloadedResources : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~ Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	//~ End UPCGManagedResource interface

	/** Drop both holds now, freeing the assets for GC / cache eviction. Idempotent. */
	void DropResidency();

	/** The resolved soft paths this resource keeps resident -- sorted, and the identity for CRC-collision-safe reuse. */
	UPROPERTY(Transient)
	TArray<FSoftObjectPath> Paths;

	/** Hard refs that GC-root the resolved assets for this resource's lifetime. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> RootedAssets;

	/** Streamable keep-alive handles (also warm in the subsystem cache). Non-UPROPERTY by design. */
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> Handles;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Collections", meta = (PCGExNodeLibraryDoc = "collections/preload-resources"))
class UPCGExPreloadResourcesSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(PreloadResources, "Resources", "Preload external assets so they stay resident across graph executions, or release previously-preloaded assets.", GetEnumName())

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Generic;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return Mode == EPCGExPreloadResourcesMode::Release
			? PCGEX_NODE_COLOR_NAME(MiscRemove)
			: PCGEX_NODE_COLOR_NAME(Misc);
	}

	FName GetEnumName() const;

	virtual bool OnlyExposePreconfiguredSettings() const override
	{
		return true;
	}

	virtual bool CanUserEditTitle() const override
	{
		return false;
	}

	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Preload assets and keep them resident, or release everything this node-type preloaded. Chosen by the palette entry (Preload Resources / Release Resources), mirroring the Constant node. */
	UPROPERTY(BlueprintReadWrite, Category = Settings)
	EPCGExPreloadResourcesMode Mode = EPCGExPreloadResourcesMode::Preload;
	
	/** Asset collections whose referenced assets should be preloaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides))
	TArray<TSoftObjectPtr<UPCGExAssetCollection>> Collections;

	/** Which categories of each collection's referenced assets to preload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides, Bitmask, BitmaskEnum = "/Script/PCGExCollections.EPCGExCollectionLoadFlags"))
	uint8 CollectionLoadFlags = static_cast<uint8>(EPCGExCollectionLoadFlags::DirectAssets) | static_cast<uint8>(EPCGExCollectionLoadFlags::SubCollections);

	/** Additional standalone assets to preload, beyond anything referenced by the collections above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides))
	TArray<TSoftObjectPtr<UObject>> Assets;

	/** Also read assets to preload from a connected input (points or attribute set). Opt-in: PCG forces the first input pin to be Required, so the input pin only exists while this is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides))
	bool bLoadFromInputs = false;

	/** Attribute on each input row holding the asset to preload (FSoftObjectPath, or an FString path). Defaults to @Last; clear it to None to use the first FSoftObjectPath attribute found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload && bLoadFromInputs", EditConditionHides))
	FPCGAttributePropertyInputSelector AssetAttribute;

	/** Register the preloaded assets for change-tracking so editing a referenced collection/asset forces a graph refresh. Editor-only effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides))
	bool bTrackResources = false;
	
	/** Print out the list of loaded assets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Mode == EPCGExPreloadResourcesMode::Preload", EditConditionHides))
	bool bLogLoadedResources = false;
};

struct FPCGExPreloadResourcesContext final : FPCGExContext
{
};

class FPCGExPreloadResourcesElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PreloadResources)

	// Resource loading marshals cache misses to the game thread and blocks; run the whole
	// (lightweight) node there so a miss can never deadlock under PCG cancellation -- exactly
	// like Destroy Actor.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return true;
	}

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
