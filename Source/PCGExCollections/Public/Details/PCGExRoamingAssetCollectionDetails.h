// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExStagingDetails.h"
#include "PCGExRoamingAssetCollectionDetails.generated.h"

class UPCGParamData;

/**
 * Builds a transient ("roaming") Omni collection from an attribute set: each row's entry type is
 * inferred from the type registry (same routing as Omni drag-drop; unclaimed assets become Generic
 * entries), and extra attributes become custom properties on the collection and its entries.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExRoamingAssetCollectionDetails : public FPCGExAssetAttributeSetDetails
{
	GENERATED_BODY()

	FPCGExRoamingAssetCollectionDetails() = default;

	/** Extra attributes exposed as custom properties: one schema entry per attribute, each row's value as an enabled entry override. The path / weight / category attributes are never mapped. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExAttributeGatherDetails PropertyAttributes;

	/** Staged bounds for entries that cannot measure their asset: Generic entries always, actors and levels when built outside the editor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FBox DefaultStagingBounds = FBox(FVector(-50.0), FVector(50.0));

	/** Suppress the warning emitted outside the editor when actor or level entries fall back to Default Staging Bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay)
	bool bQuietRuntimeStagingWarning = false;

	bool Validate(FPCGExContext* InContext) const;

	/** Build-config identity: every setting that shapes the built collection, excluding the attribute data itself. */
	FString GetConfigId() const;

	UPCGExAssetCollection* TryBuildCollection(FPCGExContext* InContext, const UPCGParamData* InAttributeSet, const bool bBuildStaging = false) const;
	UPCGExAssetCollection* TryBuildCollection(FPCGExContext* InContext, const FName InputPin, const bool bBuildStaging) const;
};
