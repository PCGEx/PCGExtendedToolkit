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

class UWorld;

namespace PCGExGetActorBounds
{
	inline const FName BoundsPinLabel = TEXT("Bounds");
}

/** Everything the actor bounds nodes share; a node only decides how the world is walked. */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc")
class UPCGExGetActorBoundsBaseSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
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

	/** Output the actors excluded by Skip Tags to a Discarded pin, shaped like the main output. They are a subset of what
	 *  the node would output without skip tags, so the bounds cull still applies. Each discarded actor then costs a bounds read. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bOutputDiscarded = false;
};

/**
 * One point per matching loaded actor (or per primitive, in Per Primitive mode), transform + bounds only, merged into
 * a single point data. Actors carrying a skip tag are dropped, or routed to an optional Discarded pin.
 * The world sweep runs on the game thread during preparation and reads cached component bounds;
 * the point write runs off-thread. No metadata is produced.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/generate/get-actor-bounds"))
class UPCGExGetActorBoundsSettings : public UPCGExGetActorBoundsBaseSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetActorBounds, "Get Actor Bounds", "One point per matching loaded actor, or per primitive (transform + bounds, no metadata), merged into a single point data, with an optional skip-tag pass and Discarded pin. A fast alternative to Get Actor Data in Get Single Point mode for exclusion volumes.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings
};

struct FPCGExGetActorBoundsContext final : FPCGExContext
{
	TArray<PCGExActorBounds::FSnapshot> Snapshots;
	TArray<PCGExActorBounds::FSnapshot> Discarded;
	TSharedPtr<PCGExData::FPointIO> Output;
	TSharedPtr<PCGExData::FPointIO> DiscardedOutput;
};

/** Resolves the selection and cull, sweeps the world on the game thread, then writes the points off-thread. */
class FPCGExGetActorBoundsBaseElement : public IPCGExElement
{
public:
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetActorBounds)
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	/** False, after logging why, when this node cannot gather from InWorld; the node then outputs nothing. */
	virtual bool CanSweep(FPCGExContext* InContext, UWorld* InWorld) const;

	/** Feeds every candidate actor of InWorld to InSweep, which filters and snapshots them. Game thread only. */
	virtual void Sweep(UWorld* InWorld, PCGExActorBounds::FSweep& InSweep) const = 0;
};

class FPCGExGetActorBoundsElement final : public FPCGExGetActorBoundsBaseElement
{
protected:
	virtual void Sweep(UWorld* InWorld, PCGExActorBounds::FSweep& InSweep) const override;
};
