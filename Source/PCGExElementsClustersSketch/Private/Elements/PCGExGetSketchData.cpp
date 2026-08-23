// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetSketchData.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Data/PCGBasePointData.h" // PCGPointDataConstants::ActorReferenceAttribute
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h" // SetDataValue
#include "Data/PCGExPointIO.h"
#include "GameFramework/Actor.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Sketch/PCGExClusterSketchComponent.h"
#include "Sketch/PCGExClusterSketchDecorator.h"
#include "Sketch/PCGExClusterSketchPrint.h"

#define LOCTEXT_NAMESPACE "PCGExGetSketchData"
#define PCGEX_NAMESPACE GetSketchData

namespace PCGExGetSketchData
{
	PCGEX_CTX_STATE(State_PrintingSketches)

	const FName SourceActorReferencesLabel = TEXT("References");

	/** Snapshots one component. Game thread only: reads live component state and its transform chain. */
	void SnapshotComponent(
		FPCGExGetSketchDataContext* Context,
		const UPCGExClusterSketchComponent* Component,
		const FSoftObjectPath& InActorPath)
	{
		FPCGExGetSketchDataContext::FSketchSource& Source = Context->Sources.AddDefaulted_GetRef();

		Source.Model = Component->GetModel(); // copied: the print runs off-thread

		Source.bHasBasis = Component->BuildBasis(Source.Basis);
		Source.Decorators.Append(Component->GetDecorators());
		Source.LocalToWorld = Component->GetComponentTransform();
		Source.ActorPath = InActorPath;
	}

	/** Walks one actor's components through the selector, snapshotting every sketch found. */
	void SnapshotActor(FPCGExGetSketchDataContext* Context, const UPCGExGetSketchDataSettings* Settings, AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return;
		}

		const FSoftObjectPath ActorPath(Actor);

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}
			if (!Settings->ComponentSelector.FilterComponent(Component))
			{
				continue;
			}
			if (Settings->bIgnorePCGGeneratedComponents && Component->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
			{
				continue;
			}

			if (const UPCGExClusterSketchComponent* Sketch = Cast<UPCGExClusterSketchComponent>(Component))
			{
				SnapshotComponent(Context, Sketch, ActorPath);
			}
		}
	}

	/** Reads actor references off one input data. Attribute sets and point data read identically.
	 *  OutUnresolved counts references that named something but resolved to no loaded actor. */
	void GatherActorsFromData(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, TArray<AActor*>& OutActors, int32& OutUnresolved)
	{
		if (!InData)
		{
			return;
		}

		const FPCGAttributePropertyInputSelector Resolved = InSelector.CopyAndFixLast(InData);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, Resolved);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(InData, Resolved);
		if (!Accessor || !Keys || Keys->GetNum() == 0)
		{
			return;
		}

		TArray<FSoftObjectPath> Paths;
		Paths.SetNum(Keys->GetNum());
		if (!Accessor->GetRange<FSoftObjectPath>(Paths, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			return;
		}

		for (const FSoftObjectPath& Path : Paths)
		{
			if (Path.IsNull())
			{
				continue;
			}
			// Already-loaded only: a streamed-out actor cannot be inspected for components.
			if (AActor* Actor = Cast<AActor>(Path.ResolveObject()))
			{
				OutActors.AddUnique(Actor);
			}
			else
			{
				++OutUnresolved;
			}
		}
	}
}

#pragma region FPCGExGetSketchDataContext

void FPCGExGetSketchDataContext::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
{
	FPCGExContext::AddExtraStructReferencedObjects(Collector);
	for (FSketchSource& Source : Sources)
	{
		Collector.AddReferencedObjects(Source.Decorators);
	}
}

#pragma endregion

#pragma region UPCGExGetSketchDataSettings

TArray<FPCGPinProperties> UPCGExGetSketchDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (ActorSource == EPCGExSketchActorSource::Input)
	{
		PCGEX_PIN_ANY(PCGExGetSketchData::SourceActorReferencesLabel, "Actor references to read sketch components from -- attribute set or points.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetSketchDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputVerticesLabel, "Point data representing vertices.", Required)
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExGetSketchDataElement

PCGEX_INITIALIZE_ELEMENT(GetSketchData)

bool FPCGExGetSketchDataElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(GetSketchData)

	PCGEX_FWD(GraphBuilderDetails)

	// --- Discover actors (game thread) ---
	TArray<AActor*> Actors;
	if (Settings->ActorSource == EPCGExSketchActorSource::Input)
	{
		int32 NumUnresolvedRefs = 0;
		for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGExGetSketchData::SourceActorReferencesLabel))
		{
			PCGExGetSketchData::GatherActorsFromData(Input.Data, Settings->ActorReferenceAttribute, Actors, NumUnresolvedRefs);
		}

		if (NumUnresolvedRefs > 0 && !Settings->bQuiet)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				         FTEXT("{0} actor reference(s) resolved to no loaded actor and were skipped."),
				         FText::AsNumber(NumUnresolvedRefs)));
		}
	}
	else
	{
		IPCGGraphExecutionSource* ExecutionSource = Context->ExecutionSource.Get();
		if (!ExecutionSource)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("No execution source to select actors from."));
			return false;
		}

		AActor* Self = ExecutionSource->GetExecutionState().GetTypedTarget<AActor>();

		TFunction<bool(const AActor*)> BoundsCheck = [](const AActor*) -> bool
		{
			return true;
		};
		if (Settings->ActorSelector.bMustOverlapSelf)
		{
			const FBox SelfBounds = Self ? PCGHelpers::GetGridBounds(Self, ExecutionSource) : FBox(ForceInit);
			BoundsCheck = [SelfBounds, ExecutionSource](const AActor* Other) -> bool
			{
				const FBox OtherBounds = Other ? PCGHelpers::GetGridBounds(Other, ExecutionSource) : FBox(ForceInit);
				return OtherBounds.IsValid && SelfBounds.IsValid && SelfBounds.Intersect(OtherBounds);
			};
		}

		TFunction<bool(const AActor*)> SelfIgnoreCheck = [](const AActor*) -> bool
		{
			return true;
		};
		if (Settings->ActorSelector.bIgnoreSelfAndChildren)
		{
			SelfIgnoreCheck = [Self](const AActor* Other) -> bool
			{
				for (const AActor* Current = Other; Current; Current = Current->GetParentActor())
				{
					if (Current == Self)
					{
						return false;
					}
				}
				return true;
			};
		}

		Actors = PCGActorSelector::FindActors(Settings->ActorSelector, ExecutionSource, BoundsCheck, SelfIgnoreCheck);
	}

	// --- Snapshot every sketch component (game thread) ---
	Settings->ComponentSelector.PrepareForFiltering();
	for (AActor* Actor : Actors)
	{
		PCGExGetSketchData::SnapshotActor(Context, Settings, Actor);
		Context->EDITOR_TrackPath(FSoftObjectPath(Actor));
	}

	if (Context->Sources.IsEmpty())
	{
		if (!Settings->bQuiet)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("No Cluster Sketch Component was found on the selected actors."));
		}
		return true; // stages nothing, but an empty selection is not a failure
	}

	Context->VtxCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxCollection->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;

	return true;
}

bool FPCGExGetSketchDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetSketchDataElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(GetSketchData)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (Context->Sources.IsEmpty())
		{
			Context->Done();
			return Context->TryComplete();
		}

		const int32 NumSources = Context->Sources.Num();
		Context->GraphBuilders.Init(nullptr, NumSources);
		Context->PrintContexts.Init(nullptr, NumSources);
		Context->VtxIOs.Init(nullptr, NumSources);

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(TaskManager)
		{
			return Context->CancelExecution(TEXT(""));
		}

		for (int32 i = 0; i < NumSources; ++i)
		{
			FPCGExGetSketchDataContext::FSketchSource& Source = Context->Sources[i];

			const TSharedPtr<PCGExData::FPointIO> VtxIO = Context->VtxCollection->Emplace_GetRef<UPCGExClusterNodesData>();
			if (!VtxIO)
			{
				return Context->CancelExecution(TEXT(""));
			}
			Context->VtxIOs[i] = VtxIO;

			PCGExSketch::FPrintRequest Request;
			Request.Model = &Source.Model;
			Request.Basis = Source.bHasBasis ? &Source.Basis : nullptr;
			Request.BuilderDetails = &Context->GraphBuilderDetails;
			Request.LocalToWorld = Source.LocalToWorld;
			Request.bQuiet = Settings->bQuiet;
			Request.Decorators.Reserve(Source.Decorators.Num());
			for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : Source.Decorators)
			{
				Request.Decorators.Add(Decorator.Get());
			}

			Context->PrintContexts[i] = MakeShared<FPCGExClusterSketchPrintContext>();
			Context->GraphBuilders[i] = PCGExSketch::PrintClusterSketch(
				Context, Request, VtxIO, TaskManager, Context->PrintContexts[i]);
		}

		Context->SetState(PCGExGetSketchData::State_PrintingSketches);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExGetSketchData::State_PrintingSketches)
	{
		const FName RefName = Settings->ActorReferenceAttributeName.IsNone()
			? PCGPointDataConstants::ActorReferenceAttribute
			: Settings->ActorReferenceAttributeName;

		for (int32 i = 0; i < Context->GraphBuilders.Num(); ++i)
		{
			const TSharedPtr<PCGExGraphs::FGraphBuilder>& Builder = Context->GraphBuilders[i];

			// A source whose model held nothing printable left a null builder. Either way the Vtx is
			// already tagged as a cluster vertex set, so it must not stage without its Edges.
			if (!Builder || !Builder->bCompiledSuccessfully)
			{
				Context->VtxIOs[i]->InitializeOutput(PCGExData::EIOInit::NoInit);
				continue;
			}

			Builder->StageEdgesOutputs();

			// Only now: the compile writes this same data, so the @Data write has to follow it.
			if (Settings->bWriteActorReference)
			{
				PCGExData::Helpers::SetDataValue<FSoftObjectPath>(Context->VtxIOs[i]->GetOut(), RefName, Context->Sources[i].ActorPath);
			}
		}

		Context->VtxCollection->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
