// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Elements/Bounds/PCGExGetActorBounds.h"

#include "PCGExGetActorBoundsWP.generated.h"

/**
 * World Partition, editor-only variant of Get Actor Bounds: reads actor descriptors, so unloaded actors
 * are included. Loaded actors are read live (descriptors only refresh on save). Warns and outputs nothing
 * outside the editor or on a non-partitioned world.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/generate/get-actor-bounds-wp"))
class UPCGExGetActorBoundsWPSettings : public UPCGExGetActorBoundsBaseSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetActorBoundsWP, "Get Actor Bounds (WP)", "Editor-only World Partition variant of Get Actor Bounds: one point per matching actor descriptor (or per primitive of loaded actors), unloaded actors included, transform and bounds only, with an optional skip-tag pass and Discarded pin. Loaded actors are read live; unloaded ones use the tags and bounds saved in their descriptor, so every actor-framed mode derives one actor-space box from it. A Bounds input culls through the partition's editor spatial hash. A Blueprint class filter loads each descriptor's base class on the game thread; native classes compare without loading.");
	virtual TArray<FText> GetNodeTitleAliases() const override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings
};

class FPCGExGetActorBoundsWPElement final : public FPCGExGetActorBoundsBaseElement
{
protected:
	virtual bool CanSweep(FPCGExContext* InContext, UWorld* InWorld) const override;
	virtual void Sweep(UWorld* InWorld, PCGExActorBounds::FSweep& InSweep) const override;
};
