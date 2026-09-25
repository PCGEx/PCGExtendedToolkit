// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExGetActorBounds.h"

#include "EngineUtils.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Data/PCGExPointIO.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "PCGExGetActorBoundsElement"
#define PCGEX_NAMESPACE GetActorBounds

#pragma region UPCGSettings interface

#if WITH_EDITOR
void UPCGExGetActorBoundsSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	PCGExActorBounds::AddStaticTrackedKeys(this, Selection, PCGExGetActorBounds::BoundsPinLabel, bMustOverlapSelf, OutKeysToSettings);
}
#endif

FString UPCGExGetActorBoundsSettings::GetAdditionalTitleInformation() const
{
	return Selection.GetTitleInformation();
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (!bUnbounded)
	{
		PCGEX_PIN_SPATIAL(PCGExGetActorBounds::BoundsPinLabel, "Only actors whose bounds overlap this data's bounds are kept.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINT(PCGPinConstants::DefaultOutputLabel, "One point per matching actor.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(GetActorBounds)

#pragma endregion

#pragma region FPCGExGetActorBoundsElement

void FPCGExGetActorBoundsElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGElement::GetDependenciesCrc(InParams, Crc);

	const UPCGExGetActorBoundsSettings* Settings = Cast<const UPCGExGetActorBoundsSettings>(InParams.Settings);
	PCGExActorBounds::CombineSelfBoundsCrc(InParams, Settings && Settings->bMustOverlapSelf, Crc);

	OutCrc = Crc;
}

bool FPCGExGetActorBoundsElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(GetActorBounds)
	check(IsInGameThread());

	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	UWorld* World = Source ? Source->GetExecutionState().GetWorld() : nullptr;
	if (!World)
	{
		return Context->CancelExecution(TEXT("No world to gather actors from."));
	}

	FPCGExActorSelectionDetails Selection = Settings->Selection;
	Selection.Init();
	if (!Selection.IsUsable())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Actor selection is empty: set a class or at least one tag."));
		return true;
	}

	PCGExActorBounds::FCull Cull;
	if (!PCGExActorBounds::ResolveCull(Context, PCGExGetActorBounds::BoundsPinLabel, Settings->bUnbounded, Settings->bMustOverlapSelf, Cull))
	{
		return true;
	}

	const AActor* Self = Selection.bIgnoreSelf ? Source->GetExecutionState().GetTypedTarget<AActor>() : nullptr;

	if (!Cull.bDisjoint)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsElement::Boot::Sweep);

		const FBox* CullBox = Cull.Get();
		for (TActorIterator<AActor> It(World, Selection.GetIterationClass()); It; ++It)
		{
			const AActor* Actor = *It;
			if (Actor == Self || !Selection.MatchesClass(Actor) || !Selection.MatchesTags(Actor->Tags))
			{
				continue;
			}

			PCGExActorBounds::FSnapshot Snapshot;
			if (PCGExActorBounds::SnapshotActor(Actor, Settings->Output, CullBox, Snapshot))
			{
				Context->Snapshots.Add(MoveTemp(Snapshot));
			}
		}
	}

#if WITH_EDITOR
	PCGExActorBounds::RegisterDynamicTracking(Context, Selection, Cull, Settings->bMustOverlapSelf, GET_MEMBER_NAME_CHECKED(UPCGExGetActorBoundsSettings, Selection));
#endif

	if (Context->Snapshots.IsEmpty())
	{
		PCGE_LOG_C(Verbose, LogOnly, InContext, FTEXT("No matching actor was found."));
		return true;
	}

	PCGExActorBounds::Sort(Context->Snapshots);

	Context->Output = PCGExData::NewPointIO(Context, PCGPinConstants::DefaultOutputLabel, 0);
	Context->Output->InitializeOutput(PCGExData::EIOInit::New);

	return true;
}

bool FPCGExGetActorBoundsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsElement::AdvanceWork);

	PCGEX_CONTEXT_AND_SETTINGS(GetActorBounds)

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
