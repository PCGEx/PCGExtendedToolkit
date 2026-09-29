// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExGetActorBounds.h"

#include "EngineUtils.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Data/PCGExPointIO.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "PCGExGetActorBoundsElement"
#define PCGEX_NAMESPACE GetActorBounds

namespace PCGExGetActorBounds
{
	/** Creates the point data the snapshots will be written to. Null when there is nothing to output. */
	TSharedPtr<PCGExData::FPointIO> PrepareOutput(FPCGExContext* InContext, const TArray<PCGExActorBounds::FSnapshot>& InSnapshots, const FName InPin)
	{
		if (InSnapshots.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<PCGExData::FPointIO> Output = PCGExData::NewPointIO(InContext, InPin, 0);
		Output->InitializeOutput(PCGExData::EIOInit::New);
		return Output;
	}

	/** Orders and writes the points, off the game thread, then stages the data. */
	void WriteOutput(FPCGExContext* InContext, TSharedPtr<PCGExData::FPointIO>& InOutOutput, TArray<PCGExActorBounds::FSnapshot>& InOutSnapshots)
	{
		if (InOutOutput)
		{
			PCGExActorBounds::WritePoints(InOutOutput->GetOut(), InOutSnapshots);
			(void)InOutOutput->StageOutput(InContext);
		}

		InOutSnapshots.Empty();
		InOutOutput.Reset();
	}
}

#pragma region UPCGExGetActorBoundsBaseSettings

#if WITH_EDITOR
void UPCGExGetActorBoundsBaseSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	PCGExActorBounds::AddStaticTrackedKeys(this, Selection, PCGExGetActorBounds::BoundsPinLabel, bMustOverlapSelf, OutKeysToSettings);
}
#endif

FString UPCGExGetActorBoundsBaseSettings::GetAdditionalTitleInformation() const
{
	return Selection.GetTitleInformation();
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsBaseSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (!bUnbounded)
	{
		PCGEX_PIN_SPATIAL(PCGExGetActorBounds::BoundsPinLabel, "Only actors whose bounds overlap this data's bounds are kept.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetActorBoundsBaseSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINT(PCGPinConstants::DefaultOutputLabel, "One point per matching actor, or per primitive in Per Primitive mode.", Normal)
	if (bOutputDiscarded)
	{
		PCGEX_PIN_POINT(PCGExCommon::Labels::OutputDiscardedLabel, "Actors that matched the selection but carry a skip tag.", Normal)
	}
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExGetActorBoundsBaseElement

void FPCGExGetActorBoundsBaseElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGElement::GetDependenciesCrc(InParams, Crc);

	const UPCGExGetActorBoundsBaseSettings* Settings = Cast<const UPCGExGetActorBoundsBaseSettings>(InParams.Settings);
	PCGExActorBounds::CombineSelfBoundsCrc(InParams, Settings && Settings->bMustOverlapSelf, Crc);

	OutCrc = Crc;
}

bool FPCGExGetActorBoundsBaseElement::CanSweep(FPCGExContext* InContext, UWorld* InWorld) const
{
	return true;
}

bool FPCGExGetActorBoundsBaseElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsBaseElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT(GetActorBounds)
	PCGEX_SETTINGS(GetActorBoundsBase)
	check(IsInGameThread());

	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	UWorld* World = Source ? Source->GetExecutionState().GetWorld() : nullptr;
	if (!World)
	{
		return Context->CancelExecution(TEXT("No world to gather actors from."));
	}

	if (!CanSweep(Context, World))
	{
		return true;
	}

	FPCGExActorSelectionDetails Selection = Settings->Selection;
	Selection.Init();
	if (!Selection.IsUsable())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Actor selection is empty: set a class or at least one tag."));
		return true;
	}

	if (!Settings->Output.IsUsable())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Bounds Source holds a value this node does not know; pick it again. No output."));
		return true;
	}

	PCGExActorBounds::FCull Cull;
	if (!PCGExActorBounds::ResolveCull(Context, PCGExGetActorBounds::BoundsPinLabel, Settings->bUnbounded, Settings->bMustOverlapSelf, Cull))
	{
		return true;
	}

	if (!Cull.bDisjoint)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsBaseElement::Boot::Sweep);

		PCGExActorBounds::FSweep ActorSweep(Selection, Settings->Output, Context->Snapshots);
		ActorSweep.Discarded = Settings->bOutputDiscarded ? &Context->Discarded : nullptr;
		ActorSweep.CullBox = Cull.Get();
		ActorSweep.Self = Selection.bIgnoreSelf ? Source->GetExecutionState().GetTypedTarget<AActor>() : nullptr;

		Sweep(World, ActorSweep);
	}

#if WITH_EDITOR
	PCGExActorBounds::RegisterDynamicTracking(Context, Selection, Cull, Settings->bMustOverlapSelf, GET_MEMBER_NAME_CHECKED(UPCGExGetActorBoundsBaseSettings, Selection));
#endif

	Context->Output = PCGExGetActorBounds::PrepareOutput(Context, Context->Snapshots, PCGPinConstants::DefaultOutputLabel);
	Context->DiscardedOutput = PCGExGetActorBounds::PrepareOutput(Context, Context->Discarded, PCGExCommon::Labels::OutputDiscardedLabel);

	if (!Context->Output && !Context->DiscardedOutput)
	{
		PCGE_LOG_C(Verbose, LogOnly, InContext, FTEXT("No matching actor was found."));
	}

	return true;
}

bool FPCGExGetActorBoundsBaseElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetActorBoundsBaseElement::AdvanceWork);

	PCGEX_CONTEXT(GetActorBounds)

	PCGExGetActorBounds::WriteOutput(Context, Context->Output, Context->Snapshots);
	PCGExGetActorBounds::WriteOutput(Context, Context->DiscardedOutput, Context->Discarded);

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#pragma region FPCGExGetActorBoundsElement

PCGEX_INITIALIZE_ELEMENT(GetActorBounds)

void FPCGExGetActorBoundsElement::Sweep(UWorld* InWorld, PCGExActorBounds::FSweep& InSweep) const
{
	for (TActorIterator<AActor> It(InWorld, InSweep.Selection.GetIterationClass()); It; ++It)
	{
		InSweep.AddActor(*It);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
