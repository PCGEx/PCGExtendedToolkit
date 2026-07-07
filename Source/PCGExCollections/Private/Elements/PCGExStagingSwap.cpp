// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSwap.h"

#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSwapElement"
#define PCGEX_NAMESPACE StagingSwap

#pragma region UPCGExStagingSwapSettings

void UPCGExStagingSwapSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExStagingSwapSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_PARAM(PCGExCollections::Labels::OutputCollectionMapLabel, "Updated collection map including the variant collections.", Normal)
	return PinProperties;
}

PCGExData::EIOInit UPCGExStagingSwapSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

#pragma endregion

#pragma region FPCGExStagingSwapContext

void FPCGExStagingSwapContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	// Unique variant paths across IOs; batch-loaded before PostLoadAssetsDependencies runs.
	TSet<FSoftObjectPath>& Required = GetRequiredAssets();
	for (const TPair<int32, TArray<FSoftObjectPath>>& Pair : VariantPathsPerIO)
	{
		Required.Append(Pair.Value);
	}
}

#pragma endregion

#pragma region FPCGExStagingSwapElement

PCGEX_INITIALIZE_ELEMENT(StagingSwap)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingSwap)

bool FPCGExStagingSwapElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingSwap)

	Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionPickUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	// The output map is a superset of the input map: every original collection plus the
	// variants that actually contribute mappings. Register originals up-front.
	Context->CollectionPickDatasetPacker = MakeShared<PCGExCollections::FPickPacker>(Context);
	for (const TPair<uint32, UPCGExAssetCollection*>& Pair : Context->CollectionPickUnpacker->GetCollections())
	{
		if (Pair.Value)
		{
			Context->CollectionPickDatasetPacker->RegisterCollection(Pair.Value);
		}
	}

	// Resolve which variant paths apply to each input IO (constant or @Data) -- attribute
	// reads only; loading happens through the asset-dependency pipeline.
	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		TArray<FSoftObjectPath> Paths;

		for (const FPCGExInputShorthandNameSoftObjectPath& VariantInput : Settings->VariantCollections)
		{
			FSoftObjectPath VariantPath;
			if (!VariantInput.TryReadDataValue(IO, VariantPath) || VariantPath.IsNull())
			{
				continue;
			}
			Paths.Add(MoveTemp(VariantPath));
		}

		if (!Paths.IsEmpty())
		{
			Context->VariantPathsPerIO.Add(IO->IOIndex, MoveTemp(Paths));
		}
	}

	return true;
}

void FPCGExStagingSwapElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(StagingSwap)

	const TMap<uint32, UPCGExAssetCollection*>& MappedCollections = Context->CollectionPickUnpacker->GetCollections();

	// Variants can be @Data-driven per input, so contribution maps are built per distinct
	// variant path (once), then assigned/merged per input IO.
	TMap<FSoftObjectPath, TSharedPtr<TMap<uint64, uint64>>> ContributionByPath;

	auto GetContribution = [&](const FSoftObjectPath& VariantPath) -> TSharedPtr<TMap<uint64, uint64>>
	{
		if (const TSharedPtr<TMap<uint64, uint64>>* Cached = ContributionByPath.Find(VariantPath))
		{
			return *Cached;
		}

		TSharedPtr<TMap<uint64, uint64>>& Contribution = ContributionByPath.Add(VariantPath); // null until proven loadable

		// Batch-loaded between RegisterAssetDependencies and this hook -- resolve only.
		UPCGExVariantCollection* Variant = Cast<UPCGExVariantCollection>(VariantPath.ResolveObject());

		if (!Variant)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("A variant collection could not be loaded (or isn't a Variant Collection) and was skipped."));
			return nullptr;
		}

		// Track every resolved variant (not just contributing ones): an edit may make a
		// currently-inert variant contribute, and that edit must retrigger generation.
		Variant->EDITOR_RegisterTrackingKeys(Context);

		Contribution = MakeShared<TMap<uint64, uint64>>();
		const uint32 VariantGUID = Variant->GetCollectionGUID();

		for (const FPCGExVariantSource& Group : Variant->Sources)
		{
			if (Group.BakedPairs.IsEmpty())
			{
				continue;
			}

			// Only groups whose source is actually present in the incoming map matter here.
			if (!MappedCollections.Contains(Group.SourceGUIDAtBake))
			{
				// The source may be present under a different GUID (re-imported/duplicated since
				// the variant was baked) -- that's a stale bake, not a missing source. Say so
				// instead of silently not swapping.
				for (const TPair<uint32, UPCGExAssetCollection*>& Pair : MappedCollections)
				{
					if (Pair.Value && FSoftObjectPath(Pair.Value) == Group.Source.ToSoftObjectPath())
					{
						PCGE_LOG(Warning, GraphAndLog, FTEXT("A variant source is present in the map but its GUID changed since the variant was baked -- mapping skipped. Re-save the variant asset to refresh it."));
						break;
					}
				}
				continue;
			}

			if (Settings->bSkipStaleMappings && Variant->IsMappingStale(Group))
			{
				PCGE_LOG(Warning, GraphAndLog, FTEXT("A variant source mapping is stale (source collection changed since the variant was saved) and was skipped. Re-save the variant asset to refresh it."));
				continue;
			}

			for (const FIntPoint& Pair : Group.BakedPairs)
			{
				// Secondary pick (e.g. material variant) is reset: it indexed the SOURCE
				// entry's micro-cache and is meaningless against the replacement entry.
				Contribution->Add(
					PCGEx::H64(Group.SourceGUIDAtBake, Pair.X),
					PCGExCollections::PickHash::Pack(VariantGUID, static_cast<uint16>(Pair.Y)));
			}
		}

		if (!Contribution->IsEmpty())
		{
			Context->CollectionPickDatasetPacker->RegisterCollection(Variant);
		}

		return Contribution;
	};

	for (const TPair<int32, TArray<FSoftObjectPath>>& PerIO : Context->VariantPathsPerIO)
	{
		TSharedPtr<TMap<uint64, uint64>> Merged;

		for (const FSoftObjectPath& VariantPath : PerIO.Value)
		{
			const TSharedPtr<TMap<uint64, uint64>> Contribution = GetContribution(VariantPath);
			if (!Contribution || Contribution->IsEmpty())
			{
				continue;
			}

			if (!Merged)
			{
				// Single-variant case shares the contribution map directly (common path).
				Merged = Contribution;
			}
			else
			{
				// Clone-on-second-contribution so a shared per-path map is never mutated.
				if (ContributionByPath.FindKey(Merged))
				{
					Merged = MakeShared<TMap<uint64, uint64>>(*Merged);
				}
				Merged->Append(*Contribution); // later slots win on conflicts
			}
		}

		if (Merged && !Merged->IsEmpty())
		{
			Context->SwapMapsPerIO.Add(PerIO.Key, Merged);
		}
	}

	if (Context->SwapMapsPerIO.IsEmpty() && !Settings->bQuietNoApplicableVariantsWarning)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("No applicable variant mappings -- points and map are forwarded unchanged."));
	}
}

bool FPCGExStagingSwapElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingSwapElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingSwap)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	{
		UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
		Context->CollectionPickDatasetPacker->PackToDataset(OutputSet);

		FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
		OutData.Data = OutputSet;
	}

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExStagingSwap::FProcessor

namespace PCGExStagingSwap
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		// Variants are resolved per input IO (constant or @Data) -- no map means this input
		// passes through untouched; the init policy already forwards/duplicates the data.
		if (const TSharedPtr<TMap<uint64, uint64>>* Found = Context->SwapMapsPerIO.Find(PointDataFacade->Source->IOIndex))
		{
			SwapMap = *Found;
		}

		if (!SwapMap || SwapMap->IsEmpty())
		{
			return true;
		}

		HashReader = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!HashReader)
		{
			return false;
		}

		HashWriter = PointDataFacade->GetWritable<int64>(PCGExCollections::Labels::Tag_EntryIdx, 0, true, PCGExData::EBufferInit::Inherit);
		if (!HashWriter)
		{
			return false;
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);
		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSwap::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const uint64 Hash = static_cast<uint64>(HashReader->Read(Index));
			if (Hash == 0)
			{
				continue;
			}

			const uint32 CollectionGUID = PCGExCollections::PickHash::GetCollectionGUID(Hash);
			const uint32 RawEntryIndex = PCGExCollections::PickHash::GetRawEntryIndex(Hash);

			if (const uint64* NewHash = SwapMap->Find(PCGEx::H64(CollectionGUID, RawEntryIndex)))
			{
				HashWriter->SetValue(Index, static_cast<int64>(*NewHash));
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
