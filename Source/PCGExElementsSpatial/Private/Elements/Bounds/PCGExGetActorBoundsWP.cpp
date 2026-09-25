// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExGetActorBoundsWP.h"

#include "PCGGraphExecutionStateInterface.h"
#include "Data/PCGExPointIO.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExGetActorBoundsWPElement"
#define PCGEX_NAMESPACE GetActorBoundsWP

#pragma region UPCGSettings interface

#if WITH_EDITOR
TArray<FText> UPCGExGetActorBoundsWPSettings::GetNodeTitleAliases() const
{
	return {FTEXT("Get Actor Bounds (World Partition)")};
}

void UPCGExGetActorBoundsWPSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	PCGExActorBounds::AddStaticTrackedKeys(this, Selection, PCGExGetActorBoundsWP::BoundsPinLabel, bMustOverlapSelf, OutKeysToSettings);
}
#endif

FString UPCGExGetActorBoundsWPSettings::GetAdditionalTitleInformation() const
{
	return Selection.GetTitleInformation();
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsWPSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (!bUnbounded)
	{
		PCGEX_PIN_SPATIAL(PCGExGetActorBoundsWP::BoundsPinLabel, "Only actors whose bounds overlap this data's bounds are kept; uses the partition's editor spatial hash.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsWPSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINT(PCGPinConstants::DefaultOutputLabel, "One point per matching actor.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(GetActorBoundsWP)

#pragma endregion

#pragma region FPCGExGetActorBoundsWPElement

void FPCGExGetActorBoundsWPElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGElement::GetDependenciesCrc(InParams, Crc);

	const UPCGExGetActorBoundsWPSettings* Settings = Cast<const UPCGExGetActorBoundsWPSettings>(InParams.Settings);
	PCGExActorBounds::CombineSelfBoundsCrc(InParams, Settings && Settings->bMustOverlapSelf, Crc);

	OutCrc = Crc;
}

bool FPCGExGetActorBoundsWPElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsWPElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

#if !WITH_EDITOR
	PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Get Actor Bounds (WP) is editor-only: actor descriptors do not exist at runtime. No output."));
	return true;
#else
	PCGEX_CONTEXT_AND_SETTINGS(GetActorBoundsWP)
	check(IsInGameThread());

	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	UWorld* World = Source ? Source->GetExecutionState().GetWorld() : nullptr;
	if (!World)
	{
		return Context->CancelExecution(TEXT("No world to gather actors from."));
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("World is not partitioned. Use Get Actor Bounds instead. No output."));
		return true;
	}

	if (!WorldPartition->GetActorDescContainerInstance())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("This world's partition exposes no actor descriptors. Use Get Actor Bounds instead. No output."));
		return true;
	}

	FPCGExActorSelectionDetails Selection = Settings->Selection;
	Selection.Init();
	if (!Selection.IsUsable())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Actor selection is empty: set a class or at least one tag."));
		return true;
	}

	PCGExActorBounds::FCull Cull;
	if (!PCGExActorBounds::ResolveCull(Context, PCGExGetActorBoundsWP::BoundsPinLabel, Settings->bUnbounded, Settings->bMustOverlapSelf, Cull))
	{
		return true;
	}

	const AActor* Self = Selection.bIgnoreSelf ? Source->GetExecutionState().GetTypedTarget<AActor>() : nullptr;

	if (!Cull.bDisjoint)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsWPElement::Boot::Sweep);

		const FBox* CullBox = Cull.Get();
		auto Visit = [&](const FWorldPartitionActorDescInstance* Desc) -> bool
		{
			if (!Selection.MatchesTags(Desc->GetTags()))
			{
				return true;
			}

			PCGExActorBounds::FSnapshot Snapshot;
			bool bKeep = false;

			// Descriptors refresh on save only, so a loaded actor is read live to pick up unsaved edits.
			const AActor* LiveActor = Desc->IsLoaded() ? Desc->GetActor(/*bEvenIfPendingKill=*/false) : nullptr;
			if (LiveActor)
			{
				if (LiveActor == Self)
				{
					return true;
				}
				bKeep = PCGExActorBounds::SnapshotActor(LiveActor, Settings->Output, CullBox, Snapshot);
			}
			else
			{
				bKeep = PCGExActorBounds::SnapshotBox(Desc->GetActorTransform(), Desc->GetEditorBounds(), Desc->GetActorName(), Settings->Output, CullBox, Snapshot);
			}

			if (bKeep)
			{
				Context->Snapshots.Add(MoveTemp(Snapshot));
			}
			return true;
		};

		// The helper applies the class filter itself, Blueprint base classes included.
		if (CullBox)
		{
			FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(WorldPartition, *CullBox, Selection.GetIterationClass(), Visit);
		}
		else
		{
			FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, Selection.GetIterationClass(), Visit);
		}
	}

	PCGExActorBounds::RegisterDynamicTracking(Context, Selection, Cull, Settings->bMustOverlapSelf, GET_MEMBER_NAME_CHECKED(UPCGExGetActorBoundsWPSettings, Selection));

	if (Context->Snapshots.IsEmpty())
	{
		PCGE_LOG_C(Verbose, LogOnly, InContext, FTEXT("No matching actor was found."));
		return true;
	}

	PCGExActorBounds::Sort(Context->Snapshots);

	Context->Output = PCGExData::NewPointIO(Context, PCGPinConstants::DefaultOutputLabel, 0);
	Context->Output->InitializeOutput(PCGExData::EIOInit::New);

	return true;
#endif
}

bool FPCGExGetActorBoundsWPElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsWPElement::AdvanceWork);

	PCGEX_CONTEXT_AND_SETTINGS(GetActorBoundsWP)

	if (Context->Output)
	{
		PCGExActorBounds::WritePoints(Context->Output->GetOut(), Context->Snapshots);
		(void)Context->Output->StageOutput(Context);
	}

	Context->Snapshots.Empty();
	Context->Output.Reset();

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
