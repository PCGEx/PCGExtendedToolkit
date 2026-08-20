// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPrintClusterSketch.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Graphs/PCGExGraphTasks.h"
#include "Helpers/PCGExAssetLoader.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchPrint.h"

#define LOCTEXT_NAMESPACE "PCGExPrintClusterSketch"
#define PCGEX_NAMESPACE PrintClusterSketch

namespace PCGExPrintClusterSketch
{
	PCGEX_CTX_STATE(State_PrintingRoots)
}

#pragma region UPCGExPrintClusterSketchSettings

TArray<FPCGPinProperties> UPCGExPrintClusterSketchSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExPrintClusterSketchContext

void FPCGExPrintClusterSketchContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	const UPCGExPrintClusterSketchSettings* Settings = GetInputSettings<UPCGExPrintClusterSketchSettings>();
	if (!Settings)
	{
		return;
	}

	if (Settings->Sketch.Input == EPCGExInputValueType::Constant)
	{
		if (Settings->Sketch.Constant.IsValid())
		{
			AddAssetDependency(Settings->Sketch.Constant);
		}
	}
	else if (SketchLoader)
	{
		SketchLoader->AddAssetDependencies();
	}
}

#pragma endregion

#pragma region FPCGExPrintClusterSketchElement

PCGEX_INITIALIZE_ELEMENT(PrintClusterSketch)

bool FPCGExPrintClusterSketchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PrintClusterSketch)

	if (Context->MainPoints->Pairs.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing targets."))
		return false;
	}

	Context->TargetsDataFacade = MakeShared<PCGExData::FFacade>(Context->MainPoints->Pairs[0].ToSharedRef());

	PCGEX_FWD(GraphBuilderDetails)

	PCGEX_FWD(TransformDetails)
	if (!Context->TransformDetails.Init(Context, Context->TargetsDataFacade.ToSharedRef()))
	{
		return false;
	}

	PCGEX_FWD(TargetsAttributesToClusterTags)
	if (!Context->TargetsAttributesToClusterTags.Init(Context, Context->TargetsDataFacade))
	{
		return false;
	}

	Context->TargetsForwardHandler = Settings->TargetsForwarding.GetHandler(Context->TargetsDataFacade);

	// Attribute-driven sketches resolve through the shared asset loader, which discovers every unique
	// path now and hands them to the context's normal asset-loading phase.
	if (Settings->Sketch.Input == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->Sketch.Attribute)

		TArray<FName> Names = {Settings->Sketch.Attribute};
		Context->SketchLoader = MakeShared<PCGEx::TAssetLoader<UPCGExClusterSketch>>(Context, Context->MainPoints.ToSharedRef(), Names);
		if (!Context->SketchLoader->Discover())
		{
			return Context->CancelExecution(TEXT("Failed to find any Cluster Sketch to load."));
		}
	}
	else if (!Settings->Sketch.Constant.IsValid())
	{
		return Context->CancelExecution(TEXT("Invalid Cluster Sketch constant."));
	}

	Context->SketchIdx.Init(-1, Context->MainPoints->Pairs[0]->GetNum());

	Context->RootVtx = MakeShared<PCGExData::FPointIOCollection>(Context); // Pinless: roots are never staged

	Context->VtxChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxChildCollection->OutputPin = Settings->GetMainOutputPin();

	Context->EdgeChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->EdgeChildCollection->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;

	return true;
}

void FPCGExPrintClusterSketchElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(PrintClusterSketch)

	if (Context->SketchLoader)
	{
		Context->SketchLoader->Finalize();
	}
	else if (Settings->Sketch.Constant.IsValid())
	{
		Context->ConstantSketch = TSoftObjectPtr<UPCGExClusterSketch>(Settings->Sketch.Constant).Get();
	}
}

bool FPCGExPrintClusterSketchElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPrintClusterSketchElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PrintClusterSketch)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->AdvancePointsIO();

		const int32 NumTargets = Context->SketchIdx.Num();

		// --- Resolve each target's sketch, deduplicated into UniqueSketches ---
		TMap<TObjectPtr<UPCGExClusterSketch>, int32> SketchToIndex;
		int32 NumUnresolved = 0;

		auto ResolveIndex = [&](UPCGExClusterSketch* InSketch) -> int32
		{
			if (!InSketch)
			{
				return -1;
			}
			if (const int32* Existing = SketchToIndex.Find(InSketch))
			{
				return *Existing;
			}
			const int32 NewIndex = Context->UniqueSketches.Add(InSketch);
			SketchToIndex.Add(InSketch, NewIndex);
			return NewIndex;
		};

		if (Context->SketchLoader)
		{
			const TSharedPtr<TArray<PCGExValueHash>> Keys = Context->SketchLoader->GetKeys(Context->CurrentIO->IOIndex);
			for (int32 i = 0; i < NumTargets; ++i)
			{
				UPCGExClusterSketch* Resolved = nullptr;
				if (Keys && Keys->IsValidIndex(i))
				{
					if (const TObjectPtr<UPCGExClusterSketch>* Found = Context->SketchLoader->GetAsset((*Keys)[i]))
					{
						Resolved = Found->Get();
					}
				}
				Context->SketchIdx[i] = ResolveIndex(Resolved);
				if (Context->SketchIdx[i] == -1)
				{
					++NumUnresolved;
				}
			}
		}
		else
		{
			const int32 ConstantIndex = ResolveIndex(Context->ConstantSketch);
			if (ConstantIndex == -1)
			{
				return Context->CancelExecution(TEXT("Cluster Sketch constant could not be loaded."));
			}
			for (int32& Index : Context->SketchIdx)
			{
				Index = ConstantIndex;
			}
		}

		if (NumUnresolved > 0 && !Settings->bQuiet)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				         FTEXT("{0} target(s) have no valid Cluster Sketch and were skipped."),
				         FText::AsNumber(NumUnresolved)));
		}

		if (Context->UniqueSketches.IsEmpty())
		{
			return Context->CancelExecution(TEXT("No Cluster Sketch could be resolved from the targets."));
		}

		// Dynamic tracking: editing a printed sketch -- or anything it references -- re-executes the
		// component. TAssetLoader does no tracking of its own, and a per-point attribute path is
		// otherwise invisible to the tracker (only a constant would ever be seen).
		TArray<FSoftObjectPath> NestedDependencies;
		for (const TObjectPtr<UPCGExClusterSketch>& Sketch : Context->UniqueSketches)
		{
			Context->EDITOR_TrackPath(FSoftObjectPath(Sketch));
			Sketch->CollectAssetDependencies(NestedDependencies);
		}

		// A sketch's own soft references (snap provider, decorators) are only knowable once the sketches
		// themselves are loaded, i.e. after the context's asset phase -- so they load here, once, before
		// any print reads them.
		if (!NestedDependencies.IsEmpty())
		{
			const TSharedPtr<TSet<FSoftObjectPath>> UniqueNested = MakeShared<TSet<FSoftObjectPath>>(NestedDependencies);
			for (const FSoftObjectPath& NestedPath : *UniqueNested)
			{
				Context->EDITOR_TrackPath(NestedPath);
			}
			PCGExHelpers::LoadBlocking_AnyThread(UniqueNested, Context);
		}

		// --- Print one shared root per unique sketch ---
		const int32 NumUnique = Context->UniqueSketches.Num();
		Context->GraphBuilders.Init(nullptr, NumUnique);
		Context->PrintContexts.Init(nullptr, NumUnique);

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(TaskManager)
		{
			return Context->CancelExecution(TEXT(""));
		}

		for (int32 i = 0; i < NumUnique; ++i)
		{
			const TSharedPtr<PCGExData::FPointIO> RootIO = Context->RootVtx->Emplace_GetRef<UPCGExClusterNodesData>();
			if (!RootIO)
			{
				return Context->CancelExecution(TEXT(""));
			}

			Context->PrintContexts[i] = MakeShared<FPCGExClusterSketchPrintContext>();
			// The asset assembles its own print request (snap provider + decorators) -- consumers only
			// ever hand it an IO and receive the finished pair.
			Context->GraphBuilders[i] = Context->UniqueSketches[i]->Print(
				Context, RootIO, TaskManager, Context->PrintContexts[i],
				&Context->GraphBuilderDetails, Settings->bQuiet);
		}

		Context->SetState(PCGExPrintClusterSketch::State_PrintingRoots);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExPrintClusterSketch::State_PrintingRoots)
	{
		Context->SetState(PCGExGraphs::States::State_WritingClusters);

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(TaskManager)
		{
			return Context->CancelExecution(TEXT(""));
		}

		const int32 NumTargets = Context->SketchIdx.Num();
		for (int32 i = 0; i < NumTargets; ++i)
		{
			const int32 SketchIndex = Context->SketchIdx[i];
			if (SketchIndex == -1 || !Context->GraphBuilders.IsValidIndex(SketchIndex))
			{
				continue;
			}
			PCGEX_LAUNCH(
				PCGExGraphTask::FCopyGraphToPoint, i, Context->CurrentIO, Context->GraphBuilders[SketchIndex],
				Context->VtxChildCollection, Context->EdgeChildCollection, &Context->TransformDetails,
				&Context->TargetsAttributesToClusterTags, Context->TargetsForwardHandler)
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExGraphs::States::State_WritingClusters)
	{
		Context->VtxChildCollection->StageOutputs();
		Context->EdgeChildCollection->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
