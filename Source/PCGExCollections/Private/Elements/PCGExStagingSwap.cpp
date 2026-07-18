// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSwap.h"

#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExAssetLoader.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSwapElement"
#define PCGEX_NAMESPACE StagingSwap

#pragma region UPCGExStagingSwapSettings

void UPCGExStagingSwapSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
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

	// All-uniform fast path.
	for (const TPair<int32, TArray<FSoftObjectPath>>& Pair : VariantPathsPerIO)
	{
		Required.Append(Pair.Value);
	}

	// Mixed path: uniform-slot paths (per-point placeholders are null) + per-point loaders' paths.
	for (const TPair<int32, TArray<FSoftObjectPath>>& Pair : UniformSlotPathsPerIO)
	{
		for (const FSoftObjectPath& Path : Pair.Value)
		{
			if (!Path.IsNull()) { Required.Add(Path); }
		}
	}

	for (const TSharedPtr<PCGEx::TAssetLoader<UPCGExVariantCollection>>& Loader : SlotLoaders)
	{
		if (Loader) { Loader->AddAssetDependencies(); }
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
	Context->CollectionPickUnpacker->RegisterCollectionsTo(*Context->CollectionPickDatasetPacker);

	// Classify slots: constant/@Data (CanSupportDataOnly) resolve one path per IO; a per-point,
	// non-@Data attribute gets its own loader (one per slot keeps per-point keys isolated).
	const int32 NumSlots = Settings->VariantCollections.Num();
	Context->SlotLoaders.Init(nullptr, NumSlots);

	for (int32 i = 0; i < NumSlots; i++)
	{
		if (Settings->VariantCollections[i].CanSupportDataOnly())
		{
			continue;
		}

		Context->bHasPerPointSlots = true;

		TArray<FName> Names = {Settings->VariantCollections[i].Attribute};
		TSharedPtr<PCGEx::TAssetLoader<UPCGExVariantCollection>> Loader = MakeShared<PCGEx::TAssetLoader<UPCGExVariantCollection>>(Context, Context->MainPoints.ToSharedRef(), Names);

		// Paths only -- assets load through the dependency pipeline, like the uniform slots.
		Loader->Discover();
		Context->SlotLoaders[i] = Loader;
	}

	if (!Context->bHasPerPointSlots)
	{
		// All-uniform (current behavior): resolve each input's variant path list in slot order.
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
	}
	else
	{
		// Mixed: resolve uniform slots per IO into their slot positions (per-point slots left null) so
		// PostLoad can assemble ordered layers. Same per-IO TryReadDataValue as the fast path.
		Context->SlotContributionByHash.Init(nullptr, NumSlots);

		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
		{
			TArray<FSoftObjectPath> Paths;
			Paths.Init(FSoftObjectPath(), NumSlots);

			for (int32 i = 0; i < NumSlots; i++)
			{
				if (Context->SlotLoaders[i])
				{
					continue; // per-point slot -- resolved via the loader
				}

				FSoftObjectPath VariantPath;
				if (Settings->VariantCollections[i].TryReadDataValue(IO, VariantPath) && !VariantPath.IsNull())
				{
					Paths[i] = MoveTemp(VariantPath);
				}
			}

			Context->UniformSlotPathsPerIO.Add(IO->IOIndex, MoveTemp(Paths));
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

	if (!Context->bHasPerPointSlots)
	{
		// All-uniform fast path: merge each IO's slot contributions into a single per-IO map.
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

		return;
	}

	// Per-point path: build each per-point slot's hash -> contribution once, from its loaded variants.
	for (int32 i = 0; i < Context->SlotLoaders.Num(); i++)
	{
		const TSharedPtr<PCGEx::TAssetLoader<UPCGExVariantCollection>> Loader = Context->SlotLoaders[i];
		if (!Loader)
		{
			continue;
		}

		// Assets were batch-loaded between RegisterAssetDependencies and here; resolve them into AssetsMap.
		Loader->Finalize();

		TSharedPtr<TMap<PCGExValueHash, TSharedPtr<TMap<uint64, uint64>>>> ByHash = MakeShared<TMap<PCGExValueHash, TSharedPtr<TMap<uint64, uint64>>>>();

		for (const TPair<PCGExValueHash, TObjectPtr<UPCGExVariantCollection>>& Pair : Loader->AssetsMap)
		{
			if (!Pair.Value)
			{
				continue;
			}

			// GetContribution caches by path, so a variant shared across slots resolves/registers once.
			const TSharedPtr<TMap<uint64, uint64>> Contribution = GetContribution(FSoftObjectPath(Pair.Value.Get()));
			if (Contribution && !Contribution->IsEmpty())
			{
				ByHash->Add(Pair.Key, Contribution);
			}
		}

		Context->SlotContributionByHash[i] = ByHash;
	}

	// Assemble ordered layers per IO -- uniform slots as fixed maps, per-point slots as (keys + ByHash).
	const int32 NumSlots = Settings->VariantCollections.Num();
	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		const int32 IOIndex = IO->IOIndex;
		const TArray<FSoftObjectPath>* UniformPaths = Context->UniformSlotPathsPerIO.Find(IOIndex);

		TArray<FPCGExStagingSwapVariantLayer> Layers;

		for (int32 i = 0; i < NumSlots; i++)
		{
			if (Context->SlotLoaders[i])
			{
				const TSharedPtr<TMap<PCGExValueHash, TSharedPtr<TMap<uint64, uint64>>>> ByHash = Context->SlotContributionByHash[i];
				if (!ByHash || ByHash->IsEmpty())
				{
					continue;
				}

				// Null keys means the loader found no variant attribute on this IO -- slot inert here.
				const TSharedPtr<TArray<PCGExValueHash>> PerPointKeys = Context->SlotLoaders[i]->GetKeys(IOIndex);
				if (!PerPointKeys)
				{
					continue;
				}

				FPCGExStagingSwapVariantLayer& Layer = Layers.Emplace_GetRef();
				Layer.PerPointKeys = PerPointKeys;
				Layer.PerPointContribution = ByHash;
			}
			else
			{
				const FSoftObjectPath Path = UniformPaths ? (*UniformPaths)[i] : FSoftObjectPath();
				if (Path.IsNull())
				{
					continue;
				}

				const TSharedPtr<TMap<uint64, uint64>> Contribution = GetContribution(Path);
				if (!Contribution || Contribution->IsEmpty())
				{
					continue;
				}

				Layers.Emplace_GetRef().Uniform = Contribution;
			}
		}

		if (!Layers.IsEmpty())
		{
			Context->LayersPerIO.Add(IOIndex, MoveTemp(Layers));
		}
	}

	if (Context->LayersPerIO.IsEmpty() && !Settings->bQuietNoApplicableVariantsWarning)
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

		const int32 IOIndex = PointDataFacade->Source->IOIndex;

		// No mapping for this input -> pass through untouched (the init policy already forwarded/duplicated it).
		if (Context->bHasPerPointSlots)
		{
			Layers = Context->LayersPerIO.Find(IOIndex);
			if (!Layers || Layers->IsEmpty())
			{
				return true;
			}
		}
		else
		{
			if (const TSharedPtr<TMap<uint64, uint64>>* Found = Context->SwapMapsPerIO.Find(IOIndex))
			{
				SwapMap = *Found;
			}

			if (!SwapMap || SwapMap->IsEmpty())
			{
				return true;
			}
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

		if (SwapMap)
		{
			// All-uniform fast path: one map governs the whole IO.
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

				if (const uint64* NewHash = SwapMap->Find(PCGExCollections::PickHash::GetEntryKey(Hash)))
				{
					HashWriter->SetValue(Index, static_cast<int64>(*NewHash));
				}
			}

			return;
		}

		// Per-point path: walk this IO's variant layers in slot order, keeping the last that remaps the pick.
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

			const uint64 Key = PCGExCollections::PickHash::GetEntryKey(Hash);

			const uint64* NewHash = nullptr;
			for (const FPCGExStagingSwapVariantLayer& Layer : *Layers)
			{
				const TMap<uint64, uint64>* Contribution = Layer.ResolveContribution(Index);
				if (!Contribution)
				{
					continue;
				}

				if (const uint64* Found = Contribution->Find(Key))
				{
					NewHash = Found;
				}
			}

			if (NewHash)
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
