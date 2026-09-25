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

#include "PCGExGetActorBoundsWP.generated.h"

namespace PCGExGetActorBoundsWP
{
	inline const FName BoundsPinLabel = TEXT("Bounds");
}

/**
 * World Partition, editor-only variant of Get Actor Bounds: reads actor descriptors, so unloaded actors
 * are included. Loaded actors are read live (descriptors only refresh on save). Warns and outputs nothing
 * outside the editor or on a non-partitioned world.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/generate/get-actor-bounds-wp"))
class UPCGExGetActorBoundsWPSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExGetActorBoundsWPElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetActorBoundsWP, "Get Actor Bounds (WP)", "Editor-only, World Partition only. One point per matching actor from the partition's actor descriptors (unloaded actors included), transform + bounds, no metadata. Loaded actors are read live; unloaded ones use the bounds saved in their descriptor.");
	virtual TArray<FText> GetNodeTitleAliases() const override;

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
	/** Which actors to gather. A Blueprint class filter loads each descriptor's base class on the game thread;
	 *  native classes compare without loading. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExActorSelectionDetails Selection;

	/** How each actor becomes a point. Unloaded actors only have a world box, so Actor Space is derived from it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Output"))
	FPCGExActorBoundsOutputDetails Output;

	/** Gather every matching actor in the partition. When off, a required Bounds input appears and only actors
	 *  overlapping it are kept, through the partition's editor spatial hash (wire the Input node for the
	 *  component's own bounds). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bUnbounded = true;

	/** Additionally require actors to overlap the executing component bounds. Independent of the Bounds input.
	 *  Adds the component bounds to the cache key, so each partitioned cell executes on its own. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMustOverlapSelf = false;
};

struct FPCGExGetActorBoundsWPContext final : FPCGExContext
{
	TArray<PCGExActorBounds::FSnapshot> Snapshots;
	TSharedPtr<PCGExData::FPointIO> Output;
};

class FPCGExGetActorBoundsWPElement final : public IPCGExElement
{
public:
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetActorBoundsWP)
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
