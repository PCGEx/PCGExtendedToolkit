// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Bounds/PCGExActorBounds.h"

#include "PCGExGetActorBounds.generated.h"

namespace PCGExGetActorBounds
{
	inline const FName BoundsPinLabel = TEXT("Bounds");
}

/**
 * One point per matching loaded actor, transform + bounds only, merged into a single point data.
 * The world sweep runs on the game thread during preparation and reads cached component bounds;
 * the point write runs off-thread. No metadata is produced.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/generate/get-actor-bounds"))
class UPCGExGetActorBoundsSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExGetActorBoundsElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetActorBounds, "Get Actor Bounds", "One point per matching loaded actor (transform + bounds, no metadata), merged into a single point data. A fast alternative to Get Actor Data in Get Single Point mode for exclusion volumes.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscAdd);
	}

	virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif
	virtual FString GetAdditionalTitleInformation() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Which actors to gather. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExActorSelectionDetails Selection;

	/** How each actor becomes a point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Output"))
	FPCGExActorBoundsOutputDetails Output;

	/** Gather every matching actor in the world. When off, a required Bounds input appears and only actors
	 *  overlapping it are kept (wire the Input node for the component's own bounds). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bUnbounded = true;

	/** Additionally require actors to overlap the executing component bounds. Independent of the Bounds input.
	 *  Adds the component bounds to the cache key, so each partitioned cell executes on its own. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMustOverlapSelf = false;
};

struct FPCGExGetActorBoundsContext final : FPCGExContext
{
	TArray<PCGExActorBounds::FSnapshot> Snapshots;
	TSharedPtr<PCGExData::FPointIO> Output;
};

class FPCGExGetActorBoundsElement final : public IPCGExElement
{
public:
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetActorBounds)
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
