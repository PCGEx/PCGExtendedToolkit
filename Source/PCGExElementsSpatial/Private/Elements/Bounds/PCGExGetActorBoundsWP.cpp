// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExGetActorBoundsWP.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "PCGExLog.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExGetActorBoundsWPElement"
#define PCGEX_NAMESPACE GetActorBoundsWP

#if WITH_EDITOR
namespace PCGExGetActorBoundsWP
{
	/**
	 * The engine's descriptor test for a Blueprint class (FWorldPartitionHelpers::IsActorDescClassCompatibleWith,
	 * private): the descriptor's base class, loaded, falling back to its native class when the load fails. Each base
	 * class loads once per sweep instead of once per descriptor. Game thread only.
	 */
	class FBlueprintClassTest
	{
	public:
		explicit FBlueprintClassTest(const UClass* InClass)
			: Class(InClass)
		{
		}

		bool Passes(const FWorldPartitionActorDescInstance* InDesc)
		{
			const UClass* BaseClass = InDesc->GetActorNativeClass();

			if (const FTopLevelAssetPath BasePath = InDesc->GetBaseClass(); !BasePath.IsNull())
			{
				UClass** Loaded = LoadedBaseClasses.Find(BasePath);
				if (!Loaded)
				{
					UClass* LoadedClass = LoadClass<AActor>(nullptr, *BasePath.ToString(), nullptr, LOAD_None, nullptr);
					if (!LoadedClass)
					{
						UE_LOG(LogPCGEx, Warning, TEXT("Get Actor Bounds (WP): failed to load actor base class %s; its native class is tested instead."), *BasePath.ToString());
					}
					Loaded = &LoadedBaseClasses.Add(BasePath, LoadedClass);
				}

				if (*Loaded)
				{
					BaseClass = *Loaded;
				}
			}

			return BaseClass && BaseClass->IsChildOf(Class);
		}

	private:
		const UClass* Class = nullptr;

		/** Null value: the load failed, the descriptor's native class stands in. */
		TMap<FTopLevelAssetPath, UClass*> LoadedBaseClasses;
	};
}
#endif

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

	// AActor when the selection has no class filter.
	UClass* SelectionClass = InSweep.Selection.GetIterationClass();

	// The helper tests a Blueprint class by loading the base class of every descriptor. It is given AActor instead and the
	// same test runs below with each base class loaded once. No narrower native class is safe to pass: a descriptor keeps
	// the native class it was saved with, which goes stale when a Blueprint is reparented.
	const bool bBlueprintFilter = !SelectionClass->IsNative();
	UClass* IterationClass = bBlueprintFilter ? AActor::StaticClass() : SelectionClass;

	PCGExGetActorBoundsWP::FBlueprintClassTest BlueprintTest(SelectionClass);

	auto Visit = [&InSweep, &BlueprintTest, SelectionClass, bBlueprintFilter](const FWorldPartitionActorDescInstance* Desc) -> bool
	{
		// Before IsLoaded, which resolves the actor path of every unloaded descriptor it is asked about.
		if (bBlueprintFilter && !BlueprintTest.Passes(Desc))
		{
			return true;
		}

		// Descriptors refresh on save only, so a loaded actor is read live (tags, class and bounds) to pick up unsaved edits.
		if (const AActor* LiveActor = Desc->IsLoaded() ? Desc->GetActor(/*bEvenIfPendingKill=*/false) : nullptr)
		{
			if (LiveActor->IsA(SelectionClass))
			{
				InSweep.AddActor(LiveActor);
			}
			return true;
		}

		// Same outcome as a loaded editor-only actor, whose primitives are all skipped: no bounds.
		const bool bNoBounds = InSweep.Output.bIgnoreEditorOnly && Desc->GetActorIsEditorOnly();
		InSweep.AddBox(Desc->GetTags(), Desc->GetActorTransform(), bNoBounds ? FBox(ForceInit) : Desc->GetEditorBounds());
		return true;
	};

	if (InSweep.CullBox)
	{
		FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(WorldPartition, *InSweep.CullBox, IterationClass, Visit);
	}
	else
	{
		FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, IterationClass, Visit);
	}
#endif
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
