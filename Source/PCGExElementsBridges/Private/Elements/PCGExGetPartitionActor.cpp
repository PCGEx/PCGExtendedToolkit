// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetPartitionActor.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGParamData.h"
#include "Data/PCGBasePointData.h" // PCGPointDataConstants
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGHelpers.h" // ComputeSeedFromPosition
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "Engine/World.h"
#include "UObject/UnrealType.h" // FPropertyChangedEvent

#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExMainThreadPoll.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGetPartitionActor"
#define PCGEX_NAMESPACE GetPartitionActor

namespace PCGExGetPartitionActor
{
	using FRequest = FPCGExGetPartitionActorContext::FRequest;
	using FSlot = FPCGExGetPartitionActorContext::FSlot;

	// Game thread. One request per descriptor source and query, in that order. InObject is null for the executing component.
	void BuildRequests(FPCGExGetPartitionActorContext* Context, const UPCGExGetPartitionActorSettings* Settings, FSlot& InOutSlot, const UObject* InObject)
	{
		IPCGGraphExecutionSource* ExecutionSource = Context->ExecutionSource.Get();
		const UPCGComponent* Executing = Cast<UPCGComponent>(ExecutionSource);

		TArray<PCGExPartitionActors::FDescriptorSource> Sources;
		if (InObject)
		{
			PCGExPartitionActors::AppendSources(InObject, Sources);
		}
		else
		{
			// The original component when there is one: a local component can only describe its own grid size.
			const UPCGComponent* Original = ExecutionSource ? Cast<UPCGComponent>(ExecutionSource->GetExecutionState().GetOriginalSource()) : nullptr;
			PCGExPartitionActors::AppendSources(Original ? Original : Executing, Sources);

			// No component at all: size, 2D and runtime still key every partition actor that carries no layers.
			if (Sources.IsEmpty()) { Sources.Emplace(); }
		}

		InOutSlot.bResolved = true;
		InOutSlot.bNoSource = Sources.IsEmpty();

		const bool bFallback2D = Executing ? Executing->Use2DGrid() : false;

		InOutSlot.Requests.Reserve(Sources.Num() * Settings->Partitions.Num());
		for (const PCGExPartitionActors::FDescriptorSource& Source : Sources)
		{
			for (const FPCGExPartitionQuery& Query : Settings->Partitions)
			{
				const uint32 GridSize = PCGExPartitionGrid::ResolveGridSize(Query, Executing);

				FRequest Request;
				if (!Source.MakeDescriptor(GridSize, Query, Settings->Kind, bFallback2D, Request.Descriptor)) { continue; }

				// The descriptor's 2D flag, not the query's: it is the one the engine placed the partition actors with.
				Request.Cell = PCGExPartitionGrid::MakeCell(GridSize, Request.Descriptor.Is2DGrid(), Query.Offset, Context->Anchor);
				InOutSlot.Requests.Add(MoveTemp(Request));
			}
		}
	}

	// Game thread. One pass over what is still missing; true when nothing is left to wait for.
	bool Advance(FPCGExGetPartitionActorContext* Context, const UPCGExGetPartitionActorSettings* Settings, const bool bCheckStreaming)
	{
		const UWorld* World = Context->World.Get();
		if (!World) { return true; }

		// Nothing loads on its own outside a game world: a reference that does not resolve there never will.
		const bool bReferencesMayLoad = World->IsGameWorld();
		bool bAnyPending = false;

		for (FSlot& Slot : Context->Slots)
		{
			if (!Slot.bResolved)
			{
				const UObject* Object = Slot.Reference.ResolveObject();
				if (!Object)
				{
					if (bReferencesMayLoad) { bAnyPending = true; }
					continue;
				}

				BuildRequests(Context, Settings, Slot, Object);
			}

			for (FRequest& Request : Slot.Requests)
			{
				if (!Request.bPending) { continue; }

				if (const APCGPartitionActor* PartitionActor = PCGExPartitionActors::Find(World, Request.Descriptor, Request.Cell.Coord))
				{
					Request.Actor = FSoftObjectPath(PartitionActor);
					Request.Location = PartitionActor->GetActorLocation();
					Request.bPending = false;
				}
				// Serialized only: the scheduler assigns a runtime generated partition actor once streaming is complete.
				else if (bCheckStreaming && !Request.Descriptor.IsRuntime() && PCGExPartitionActors::IsCellStreamedIn(World, Request.Cell))
				{
					Request.bPending = false;
				}
				else
				{
					bAnyPending = true;
				}
			}
		}

		return !bAnyPending;
	}

	// The output attributes, on either output shape. ActorReference is marked as the last one, like the engine's own
	// actor reference outputs: a downstream '@Last' selector must not land on PartitionId.
	struct FRowWriter
	{
		UPCGMetadata* Metadata = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* ActorAttr = nullptr;
		FPCGMetadataAttribute<FString>* IdAttr = nullptr;

		explicit FRowWriter(UPCGData* InData)
			: Metadata(InData->MutableMetadata())
		{
			check(Metadata);

			ActorAttr = Metadata->FindOrCreateAttribute<FSoftObjectPath>(PCGPointDataConstants::ActorReferenceAttribute, FSoftObjectPath(), false, false, true);
			IdAttr = Metadata->FindOrCreateAttribute<FString>(PCGExPartitionActors::PartitionIdAttributeName, FString(), false, false, true);
			check(ActorAttr && IdAttr);

			FPCGAttributePropertySelector LastSelector;
			LastSelector.SetAttributeName(PCGPointDataConstants::ActorReferenceAttribute);
			InData->SetLastSelector(LastSelector);
		}

		// Sequential only: AddEntry and SetValue both mutate shared metadata structures.
		PCGMetadataEntryKey Add(const FRequest& InRequest) const
		{
			const PCGMetadataEntryKey Key = Metadata->AddEntry();
			ActorAttr->SetValue(Key, InRequest.Actor);
			IdAttr->SetValue(Key, PCGExPartitionGrid::FormatId(InRequest.Cell));
			return Key;
		}
	};
}

#pragma region UPCGExGetPartitionActorSettings

#if WITH_EDITOR
FLinearColor UPCGExGetPartitionActorSettings::GetNodeTitleColor() const
{
	return PCGEX_NODE_COLOR_OPTIN_NAME(Action);
}

EPCGChangeType UPCGExGetPartitionActorSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExGetPartitionActorSettings, DescriptorSource))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGExGetPartitionActorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (DescriptorSource == EPCGExPartitionDescriptorSource::Input)
	{
		PCGEX_PIN_ANY(PCGExGetPartitionActor::ReferencePinLabel, "References to the actors, partition actors or PCG components whose partitions to look up.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetPartitionActorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	switch (OutputType)
	{
	case EPCGExPartitionActorOutput::AttributeSet:
		PCGEX_PIN_PARAM(PCGPinConstants::DefaultOutputLabel, "One row per loaded partition actor: ActorReference, PartitionId.", Normal)
		break;
	case EPCGExPartitionActorOutput::Points:
		PCGEX_PIN_POINT(PCGPinConstants::DefaultOutputLabel, "One point per loaded partition actor, with ActorReference and PartitionId attributes.", Normal)
		break;
	default:
		// Pin layout path: loud but survivable, so an asset with a retired enumerator can still be opened and fixed.
		ensureMsgf(false, TEXT("Unresolvable EPCGExPartitionActorOutput (%d)"), static_cast<int32>(OutputType));
		break;
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(GetPartitionActor)

#pragma endregion

#pragma region FPCGExGetPartitionActorElement

bool FPCGExGetPartitionActorElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(GetPartitionActor)
	check(IsInGameThread());

	if (Settings->Partitions.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoPartitions", "Partitions is empty."));
		return false;
	}

	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	UWorld* World = Context->GetWorld();
	if (!World)
	{
		return Context->CancelExecution(TEXT("No world to look partition actors up in."));
	}

	Context->World = World;

	if (!PCGExPartitionGrid::TryGetAnchor(Source, Context->Anchor))
	{
		PCGE_LOG(Warning, GraphAndLog, LOCTEXT("UnresolvedAnchor", "The executing cell could not be resolved (no valid bounds); nothing was output."));
		return true;
	}

	switch (Settings->DescriptorSource)
	{
	case EPCGExPartitionDescriptorSource::ExecutingComponent:
		PCGExGetPartitionActor::BuildRequests(Context, Settings, Context->Slots.Emplace_GetRef(), nullptr);
		break;
	case EPCGExPartitionDescriptorSource::Input:
		{
			TArray<FSoftObjectPath> References;
			const bool bReadable = PCGExData::Helpers::BulkReadUniqueSoftPaths(Context->InputData, PCGExGetPartitionActor::ReferencePinLabel, Settings->ActorReferenceAttribute, References);

			if (!Settings->bQuietInvalidReferenceWarning)
			{
				if (!bReadable)
				{
					PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("MissingActorReferenceAttribute", "Reference data has no readable '{0}' attribute."), FText::FromName(Settings->ActorReferenceAttribute)));
				}
				else if (References.IsEmpty())
				{
					PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NoReference", "The Reference pin carries no reference."));
				}
			}

			Context->Slots.Reserve(References.Num());
			for (const FSoftObjectPath& Reference : References) { Context->Slots.Emplace_GetRef().Reference = Reference; }
		}
		break;
	default:
		ensureMsgf(false, TEXT("Unresolvable EPCGExPartitionDescriptorSource (%d)"), static_cast<int32>(Settings->DescriptorSource));
		return Context->CancelExecution(TEXT("Unresolvable descriptor source."));
	}

	// The streaming test only matters to the wait: without it, what is not registered now is simply missing.
	Context->bNeedsWait = !PCGExGetPartitionActor::Advance(Context, Settings, Settings->bWaitForMissingActors) && Settings->bWaitForMissingActors;

	return true;
}

bool FPCGExGetPartitionActorElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(GetPartitionActor)

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);

		if (Context->bNeedsWait)
		{
			Context->Wait = MakeShared<PCGExMT::FMainThreadPoll>(Settings->WaitTimeout);
			Context->Wait->OnPollCallback = [CtxHandle = Context->GetWeakSelfHandle()]()
			{
				const FPCGContext::FSharedContext<FPCGExGetPartitionActorContext> SharedContext(CtxHandle);
				FPCGExGetPartitionActorContext* Ctx = SharedContext.Get();

				// A released context has nothing left to wait for.
				return !Ctx || PCGExGetPartitionActor::Advance(Ctx, Ctx->GetInputSettings<UPCGExGetPartitionActorSettings>(), /*bCheckStreaming=*/true);
			};

			// Refused when the work is already cancelled; what Boot found is then output as-is.
			if (Context->GetTaskManager()->TryRegisterHandle(Context->Wait)) { return false; }
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		// Resolved requests in slot, then source, then query order; a partition actor reached twice is output once.
		TArray<const PCGExGetPartitionActor::FRequest*> Resolved;
		TSet<FSoftObjectPath> Seen;
		TArray<FString> MissingIds;
		TArray<FString> InvalidReferences;

		for (const PCGExGetPartitionActor::FSlot& Slot : Context->Slots)
		{
			if (!Slot.bResolved || Slot.bNoSource)
			{
				InvalidReferences.Add(FString::Printf(TEXT("'%s'"), *Slot.Reference.ToString()));
				continue;
			}

			for (const PCGExGetPartitionActor::FRequest& Request : Slot.Requests)
			{
				if (Request.Actor.IsNull())
				{
					MissingIds.AddUnique(FString::Printf(TEXT("'%s'"), *PCGExPartitionGrid::FormatId(Request.Cell)));
					continue;
				}

				bool bAlreadySeen = false;
				Seen.Add(Request.Actor, &bAlreadySeen);
				if (!bAlreadySeen) { Resolved.Add(&Request); }
			}
		}

		if (Context->Wait && Context->Wait->HasTimedOut() && !Settings->bQuietTimeoutWarning)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("WaitTimeout", "Timed out waiting for partition actors; outputting what is loaded."));
		}

		if (!InvalidReferences.IsEmpty() && !Settings->bQuietInvalidReferenceWarning)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("InvalidReferences", "Not loaded, or nothing that carries a PCG component: {0}."), FText::FromString(FString::Join(InvalidReferences, TEXT(", ")))));
		}

		if (!MissingIds.IsEmpty())
		{
			const FText Miss = FText::Format(LOCTEXT("MissingPartitions", "No loaded partition actor for {0}."), FText::FromString(FString::Join(MissingIds, TEXT(", "))));
			if (Resolved.IsEmpty())
			{
				// Nothing loaded at all is the common unloaded-region case: the empty output is the branch point, not a warning.
				PCGE_LOG_C(Verbose, LogOnly, Context, Miss);
			}
			else if (!Settings->bQuietMissingPartitionWarning)
			{
				PCGE_LOG(Warning, GraphAndLog, Miss);
			}
		}

		switch (Settings->OutputType)
		{
		case EPCGExPartitionActorOutput::AttributeSet:
			{
				// Staged even with no row, so a downstream reader of the attribute set always has data to read.
				UPCGParamData* Out = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);

				const PCGExGetPartitionActor::FRowWriter Rows(Out);
				for (const PCGExGetPartitionActor::FRequest* Request : Resolved) { Rows.Add(*Request); }

				Context->StageOutput(Out, PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::Mutable);
			}
			break;
		case EPCGExPartitionActorOutput::Points:
			if (!Resolved.IsEmpty())
			{
				const TSharedPtr<PCGExData::FPointIO> Output = PCGExData::NewPointIO(Context, PCGPinConstants::DefaultOutputLabel, 0);

				// Only fails once the work is being torn down.
				if (!Output->InitializeOutput(PCGExData::EIOInit::New)) { return Context->CancelExecution(FString()); }

				UPCGBasePointData* OutData = Output->GetOut();
				const int32 NumPoints = Resolved.Num();

				PCGExPointArrayDataHelpers::SetNumPointsAllocated(
					OutData, NumPoints,
					EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);

				TPCGValueRange<FTransform> Transforms = OutData->GetTransformValueRange(false);
				TPCGValueRange<FVector> BoundsMin = OutData->GetBoundsMinValueRange(false);
				TPCGValueRange<FVector> BoundsMax = OutData->GetBoundsMaxValueRange(false);
				TPCGValueRange<int32> Seeds = OutData->GetSeedValueRange(false);
				TPCGValueRange<int64> MetadataEntries = OutData->GetMetadataEntryValueRange(/*bAllocate=*/true);

				const PCGExGetPartitionActor::FRowWriter Rows(OutData);
				for (int32 i = 0; i < NumPoints; i++)
				{
					const PCGExGetPartitionActor::FRequest& Request = *Resolved[i];
					const FVector HalfExtent(static_cast<double>(Request.Cell.GridSize) * 0.5);

					Transforms[i] = FTransform(Request.Location);
					BoundsMin[i] = -HalfExtent;
					BoundsMax[i] = HalfExtent;
					Seeds[i] = PCGHelpers::ComputeSeedFromPosition(Request.Location);
					MetadataEntries[i] = Rows.Add(Request);
				}

				(void)Output->StageOutput(Context);
			}
			break;
		default:
			ensureMsgf(false, TEXT("Unresolvable EPCGExPartitionActorOutput (%d)"), static_cast<int32>(Settings->OutputType));
			return Context->CancelExecution(TEXT("Unresolvable output type."));
		}

		Context->Done();
	}

	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
