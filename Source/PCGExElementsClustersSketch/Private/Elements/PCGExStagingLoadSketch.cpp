// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadSketch.h"

#include "PCGParamData.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Collections/PCGExClusterSketchCollection.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Graphs/PCGExGraphTasks.h"
#include "Helpers/PCGExAssetLoader.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchPrint.h"

#define LOCTEXT_NAMESPACE "PCGExStagingLoadSketch"
#define PCGEX_NAMESPACE StagingLoadSketch

namespace PCGExStagingLoadSketch
{
	PCGEX_CTX_STATE(State_PrintingRoots)

	/** Resolves one input's staged picks into the SHARED UniqueSketchPaths, filling its SketchIdx. Runs
	 *  in Boot so RegisterAssetDependencies, which runs after it, can hand the paths to the async load
	 *  phase. Paths dedupe across inputs, so two inputs naming one sketch print a single root. */
	bool ResolveStagedSketches(
		FPCGExStagingLoadSketchContext* Context,
		const UPCGExStagingLoadSketchSettings* Settings,
		PCGExCollections::FPickUnpacker& Unpacker,
		FPCGExStagingLoadSketchContext::FTargets& Target)
	{
		const FName EntryIdxName = Settings->GetEntryIdxAttributeName();
		PCGEX_VALIDATE_NAME_C(Context, EntryIdxName)

		const TSharedPtr<PCGExData::TBuffer<int64>> HashGetter =
			Target.Facade->GetReadable<int64>(EntryIdxName, PCGExData::EIOSide::In, false);

		if (!HashGetter)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Missing staging hash attribute. Make sure points were staged with Collection Map output."));
			return false;
		}

		// Cache verdict for a pick hash. Non-sketch and broken are distinguished because only broken
		// counts toward the warning, but BOTH must be cached or a repeated bad pick re-resolves.
		constexpr int32 NonSketchPick = -1;
		constexpr int32 BrokenPick = -2;

		// Keyed on the pick hash, not the resolved path: a repeated pick then costs one integer probe
		// instead of a full entry resolve plus an FName-pair path hash.
		TMap<int64, int32> HashToIndex;
		const int32 NumTargets = Target.SketchIdx.Num();

		for (int32 i = 0; i < NumTargets; ++i)
		{
			const int64 Hash = HashGetter->Read(i);
			if (Hash == 0 || Hash == -1)
			{
				continue;
			}

			const int32* Cached = HashToIndex.Find(Hash);
			if (!Cached)
			{
				// Ignored: secondary picks only ever come from mesh micro caches.
				int16 SecondaryIndex = 0;
				const FPCGExEntryAccessResult Result = Unpacker.ResolveEntry(Hash, SecondaryIndex);

				int32 Verdict = BrokenPick;
				if (Result.IsValid())
				{
					if (!Result.Entry->IsType(PCGExSketch::CollectionTypeId))
					{
						Verdict = NonSketchPick;
					}
					else
					{
						const FSoftObjectPath Path = static_cast<const FPCGExClusterSketchCollectionEntry*>(Result.Entry)->Sketch.ToSoftObjectPath();
						if (!Path.IsNull()) { Verdict = Context->UniqueSketchPaths.AddUnique(Path); }
					}
				}

				Cached = &HashToIndex.Add(Hash, Verdict);
			}

			if (*Cached == BrokenPick) { ++Context->NumUnresolvedTargets; }
			else { Target.SketchIdx[i] = *Cached; }
		}

		return true;
	}
}

#pragma region UPCGExStagingLoadSketchSettings

void UPCGExStagingLoadSketchSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	if (Source == EPCGExClusterSketchSource::CollectionMap)
	{
		PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from staging nodes.", Required)
	}
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExStagingLoadSketchSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExStagingLoadSketchContext

void FPCGExStagingLoadSketchContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	const UPCGExStagingLoadSketchSettings* Settings = GetInputSettings<UPCGExStagingLoadSketchSettings>();
	if (!Settings)
	{
		return;
	}

	// Runs AFTER Boot, so the CollectionMap paths Boot resolved go through the normal async load
	// phase -- no blocking load, same as the constant path.
	if (Settings->Source == EPCGExClusterSketchSource::CollectionMap)
	{
		for (const FSoftObjectPath& Path : UniqueSketchPaths)
		{
			AddAssetDependency(Path);
		}
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

#pragma region FPCGExStagingLoadSketchElement

PCGEX_INITIALIZE_ELEMENT(StagingLoadSketch)

bool FPCGExStagingLoadSketchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadSketch)

	if (Context->MainPoints->Pairs.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing targets."))
		return false;
	}

	PCGEX_FWD(GraphBuilderDetails)

	// Sized once and never resized: the copy tasks hold raw pointers into these entries.
	Context->Targets.SetNum(Context->MainPoints->Pairs.Num());

	// One unpacker for the whole node -- the Map pin is read once, not per input.
	PCGExCollections::FPickUnpacker Unpacker;
	const bool bStaged = Settings->Source == EPCGExClusterSketchSource::CollectionMap;
	if (bStaged)
	{
		Unpacker.UnpackPin(Context);
		if (!Unpacker.HasValidMapping())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
			return false;
		}
	}

	for (int32 i = 0; i < Context->Targets.Num(); ++i)
	{
		FPCGExStagingLoadSketchContext::FTargets& Target = Context->Targets[i];
		Target.Facade = MakeShared<PCGExData::FFacade>(Context->MainPoints->Pairs[i].ToSharedRef());

		Target.TransformDetails = Settings->TransformDetails;
		if (!Target.TransformDetails.Init(Context, Target.Facade.ToSharedRef()))
		{
			return false;
		}

		Target.AttributesToClusterTags = Settings->TargetsAttributesToClusterTags;
		if (!Target.AttributesToClusterTags.Init(Context, Target.Facade))
		{
			return false;
		}

		Target.ForwardHandler = Settings->TargetsForwarding.GetHandler(Target.Facade);
		Target.SketchIdx.Init(-1, Context->MainPoints->Pairs[i]->GetNum());

		if (bStaged && !PCGExStagingLoadSketch::ResolveStagedSketches(Context, Settings, Unpacker, Target))
		{
			return false;
		}
	}

	if (!bStaged && Settings->Sketch.Input == EPCGExInputValueType::Attribute)
	{
		// Attribute-driven sketches resolve through the shared asset loader, which discovers every unique
		// path now and hands them to the context's normal asset-loading phase.
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->Sketch.Attribute)

		TArray<FName> Names = {Settings->Sketch.Attribute};
		Context->SketchLoader = MakeShared<PCGEx::TAssetLoader<UPCGExClusterSketch>>(Context, Context->MainPoints.ToSharedRef(), Names);
		if (!Context->SketchLoader->Discover())
		{
			return Context->CancelExecution(TEXT("Failed to find any Cluster Sketch to load."));
		}
	}
	else if (!bStaged && !Settings->Sketch.Constant.IsValid())
	{
		return Context->CancelExecution(TEXT("Invalid Cluster Sketch constant."));
	}

	Context->RootVtx = MakeShared<PCGExData::FPointIOCollection>(Context); // Pinless: roots are never staged

	Context->VtxChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxChildCollection->OutputPin = Settings->GetMainOutputPin();

	Context->EdgeChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->EdgeChildCollection->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;

	return true;
}

void FPCGExStagingLoadSketchElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadSketch)

	// CollectionMap resolves its paths in AdvanceWork; a stale Sketch constant left over from Asset
	// mode must not be picked up here.
	if (Settings->Source == EPCGExClusterSketchSource::CollectionMap)
	{
		return;
	}

	if (Context->SketchLoader)
	{
		Context->SketchLoader->Finalize();
	}
	else if (Settings->Sketch.Constant.IsValid())
	{
		Context->ConstantSketch = TSoftObjectPtr<UPCGExClusterSketch>(Settings->Sketch.Constant).Get();
	}
}

bool FPCGExStagingLoadSketchElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingLoadSketchElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadSketch)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		// --- Resolve each target's sketch, deduplicated into UniqueSketches across ALL inputs ---
		TMap<TObjectPtr<UPCGExClusterSketch>, int32> SketchToIndex;
		int32 NumUnresolved = Context->NumUnresolvedTargets;

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

		if (Settings->Source == EPCGExClusterSketchSource::CollectionMap)
		{
			// UniqueSketches is built PARALLEL to UniqueSketchPaths -- null slots kept -- so the
			// indices Boot already stored in SketchIdx stay valid and need no remap.
			Context->UniqueSketches.Reserve(Context->UniqueSketchPaths.Num());
			for (const FSoftObjectPath& Path : Context->UniqueSketchPaths)
			{
				Context->UniqueSketches.Add(TSoftObjectPtr<UPCGExClusterSketch>(Path).Get());
			}

			for (FPCGExStagingLoadSketchContext::FTargets& Target : Context->Targets)
			{
				for (int32& Index : Target.SketchIdx)
				{
					if (Index != -1 && !Context->UniqueSketches[Index])
					{
						Index = -1;
						++NumUnresolved;
					}
				}
			}
		}
		else if (Context->SketchLoader)
		{
			for (int32 t = 0; t < Context->Targets.Num(); ++t)
			{
				FPCGExStagingLoadSketchContext::FTargets& Target = Context->Targets[t];
				const TSharedPtr<TArray<PCGExValueHash>> Keys = Context->SketchLoader->GetKeys(Context->MainPoints->Pairs[t]->IOIndex);

				for (int32 i = 0; i < Target.SketchIdx.Num(); ++i)
				{
					UPCGExClusterSketch* Resolved = nullptr;
					if (Keys && Keys->IsValidIndex(i))
					{
						if (const TObjectPtr<UPCGExClusterSketch>* Found = Context->SketchLoader->GetAsset((*Keys)[i]))
						{
							Resolved = Found->Get();
						}
					}
					Target.SketchIdx[i] = ResolveIndex(Resolved);
					if (Target.SketchIdx[i] == -1)
					{
						++NumUnresolved;
					}
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
			for (FPCGExStagingLoadSketchContext::FTargets& Target : Context->Targets)
			{
				for (int32& Index : Target.SketchIdx) { Index = ConstantIndex; }
			}
		}

		if (NumUnresolved > 0 && !Settings->bQuiet)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				         FTEXT("{0} target(s) have no valid Cluster Sketch and were skipped."),
				         FText::AsNumber(NumUnresolved)));
		}

		// A staged batch that picked no sketch at all is normal in a mixed host -- stage empty outputs
		// rather than failing. A hand-authored Asset source resolving to nothing IS a misconfiguration.
		if (Context->UniqueSketches.IsEmpty())
		{
			if (Settings->Source != EPCGExClusterSketchSource::CollectionMap)
			{
				return Context->CancelExecution(TEXT("No Cluster Sketch could be resolved from the targets."));
			}

			Context->VtxChildCollection->StageOutputs();
			Context->EdgeChildCollection->StageOutputs();
			Context->Done();
			return Context->TryComplete();
		}

		// Dynamic tracking: editing a printed sketch -- or anything it references -- re-executes the
		// component. TAssetLoader does no tracking of its own, and a per-point attribute path is
		// otherwise invisible to the tracker (only a constant would ever be seen).
		TArray<FSoftObjectPath> NestedDependencies;
		for (const TObjectPtr<UPCGExClusterSketch>& Sketch : Context->UniqueSketches)
		{
			if (!Sketch) { continue; }
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
			if (!Context->UniqueSketches[i]) { continue; }

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

		Context->SetState(PCGExStagingLoadSketch::State_PrintingRoots);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExStagingLoadSketch::State_PrintingRoots)
	{
		Context->SetState(PCGExGraphs::States::State_WritingClusters);

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(TaskManager)
		{
			return Context->CancelExecution(TEXT(""));
		}

		// OutIOIndex is a running counter across every input: StageOutputs sorts on it with an unstable
		// sort, so per-input indices restarting at 0 would make output order nondeterministic.
		int32 OutIOIndex = 0;
		for (int32 t = 0; t < Context->Targets.Num(); ++t)
		{
			FPCGExStagingLoadSketchContext::FTargets& Target = Context->Targets[t];
			const TSharedPtr<PCGExData::FPointIO>& TargetIO = Context->MainPoints->Pairs[t];

			for (int32 i = 0; i < Target.SketchIdx.Num(); ++i)
			{
				const int32 SketchIndex = Target.SketchIdx[i];
				if (SketchIndex == -1 || !Context->GraphBuilders.IsValidIndex(SketchIndex) || !Context->GraphBuilders[SketchIndex])
				{
					continue;
				}
				PCGEX_LAUNCH(
					PCGExGraphTask::FCopyGraphToPoint, i, TargetIO, Context->GraphBuilders[SketchIndex],
					Context->VtxChildCollection, Context->EdgeChildCollection, &Target.TransformDetails,
					&Target.AttributesToClusterTags, Target.ForwardHandler, OutIOIndex++)
			}
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
