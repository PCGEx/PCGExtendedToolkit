// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExGetActorBoundsWP.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExGetActorBoundsWPElement"
#define PCGEX_NAMESPACE GetActorBoundsWP

#pragma region UPCGExGetActorBoundsWPSettings

#if WITH_EDITOR
TArray<FText> UPCGExGetActorBoundsWPSettings::GetNodeTitleAliases() const
{
	return {FTEXT("Get Actor Bounds (World Partition)")};
}
#endif

PCGEX_INITIALIZE_ELEMENT(GetActorBoundsWP)

#pragma endregion

#pragma region FPCGExGetActorBoundsWPElement

bool FPCGExGetActorBoundsWPElement::CanSweep(FPCGExContext* InContext, UWorld* InWorld) const
{
#if !WITH_EDITOR
	PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Get Actor Bounds (WP) is editor-only: actor descriptors do not exist at runtime. No output."));
	return false;
#else
	const UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	if (!WorldPartition)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("World is not partitioned. Use Get Actor Bounds instead. No output."));
		return false;
	}

	if (!WorldPartition->GetActorDescContainerInstance())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("This world's partition exposes no actor descriptors. Use Get Actor Bounds instead. No output."));
		return false;
	}

	return true;
#endif
}

void FPCGExGetActorBoundsWPElement::Sweep(UWorld* InWorld, PCGExActorBounds::FSweep& InSweep) const
{
#if WITH_EDITOR
	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	check(WorldPartition);

	auto Visit = [&InSweep](const FWorldPartitionActorDescInstance* Desc) -> bool
	{
		// Descriptors refresh on save only, so a loaded actor is read live (tags and bounds) to pick up unsaved edits.
		if (const AActor* LiveActor = Desc->IsLoaded() ? Desc->GetActor(/*bEvenIfPendingKill=*/false) : nullptr)
		{
			InSweep.AddActor(LiveActor);
			return true;
		}

		// Same outcome as a loaded editor-only actor, whose primitives are all skipped: no bounds.
		const bool bNoBounds = InSweep.Output.bIgnoreEditorOnly && Desc->GetActorIsEditorOnly();
		InSweep.AddBox(Desc->GetTags(), Desc->GetActorTransform(), bNoBounds ? FBox(ForceInit) : Desc->GetEditorBounds());
		return true;
	};

	// The helper applies the class filter itself, Blueprint base classes included.
	if (InSweep.CullBox)
	{
		FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(WorldPartition, *InSweep.CullBox, InSweep.Selection.GetIterationClass(), Visit);
	}
	else
	{
		FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, InSweep.Selection.GetIterationClass(), Visit);
	}
#endif
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
