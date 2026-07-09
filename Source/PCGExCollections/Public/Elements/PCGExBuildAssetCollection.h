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
#include "Details/PCGExRoamingAssetCollectionDetails.h"

#include "PCGExBuildAssetCollection.generated.h"

class UPCGExAssetCollection;

/**
 * Managed resource owning the transient collection. PCG marks all resources unused each (re)generation; the
 * node re-marks the matching one by CRC so identical configs are reused and stale ones dropped. Config
 * disambiguates 32-bit CRC collisions.
 */
UCLASS()
class UPCGExManagedAssetCollection : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~ Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	//~ End UPCGManagedResource interface

	/** Strong ref: the collection's keep-alive for the resource's lifetime. */
	UPROPERTY(Transient)
	TObjectPtr<UPCGExAssetCollection> Collection = nullptr;

	/** Build-config identity; disambiguates CRC collisions on reuse. */
	UPROPERTY(Transient)
	FString Config;
};

/**
 * Builds a transient asset collection (with baked staging) from an input attribute set and outputs its soft
 * path, so Staging : Distribute can consume it via SourceCollection (Constant) as if it were a saved asset.
 * Inverse of Asset Collection to Set. Anchored by a UPCGExManagedAssetCollection on the component, so
 * identical inputs dedup by CRC and survive regeneration. Main-thread-only + non-cacheable (see below).
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Collections", meta = (PCGExNodeLibraryDoc = "collections/build-asset-collection"))
class UPCGExBuildAssetCollectionSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	UPCGExBuildAssetCollectionSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildAssetCollection, "Build Asset Collection", "Builds a transient asset collection from an input attribute set and outputs its soft path for a Staging : Distribute SourceCollection (Constant) override.")

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }

	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Misc); }
#endif

	// Non-cacheable so the CRC reuse/mark-used pass runs every generation (via ShouldCache).
	virtual bool IsCacheable() const override { return false; }

	// Tag the output as a soft object path so it connects to soft-path override pins (Distribute's SourceCollection/Constant).
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Which collection type to build, and which attributes hold the asset path / weight / category. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExRoamingAssetCollectionDetails AttributeSetDetails;

	/** Name of the output FSoftObjectPath attribute carrying the built collection's soft path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName OutputAttributeName = FName("Collection");
};

struct FPCGExBuildAssetCollectionContext final : FPCGExContext
{
};

class FPCGExBuildAssetCollectionElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildAssetCollection)

	// GT-only (NewObject + resource registration + blocking staging load); running there also dodges the
	// cancellation deadlock.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
