// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Asset Staging - Picks assets from collections and assigns them to points.
// Handles weighted distribution, fitting/scaling, material picking, and socket extraction.

#include "Elements/PCGExStagingDistribute.h"

#if WITH_EDITOR
#include "PCGExSubSystem.h"
#endif

#include "PCGExLog.h"
#include "PCGGraph.h"
#include "PCGParamData.h"
#include "Collections/PCGExMeshCollection.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Factories/PCGExFactories.h"
#include "Helpers/PCGExAssetLoader.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Helpers/PCGExSocketHelpers.h"
#include "Selectors/PCGExSelectorClassic.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorSharedData.h"


#define LOCTEXT_NAMESPACE "PCGExAssetStagingElement"
#define PCGEX_NAMESPACE AssetStaging

#pragma region UPCGExAssetStagingSettings

UPCGExAssetStagingSettings::UPCGExAssetStagingSettings()
{
	CacheLoadedResources = EPCGExOptionState::Enabled;
}

#if WITH_EDITOR
void UPCGExAssetStagingSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	// Resolve the deferred selector default ONCE, and only while still Unset so explicit user choices
	// are never overwritten. Nodes predating the Unset default (< 1.75.19) keep the legacy inline
	// behavior; newer nodes adopt the external-factory default. PCGExDataVersion is resolved in Serialize.
	if (SelectorMode == EPCGExSelectorMode::Unset)
	{
		SelectorMode = EPCGExSelectorMode::External;
		PCGEX_IF_VERSION_LOWER(1, 75, 19)
		{
			SelectorMode = EPCGExSelectorMode::Legacy;
		}
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 4)
	{
		// Rewire the old override pins onto the shorthand's sub-pins (the value migrates in PCGExApplyDeprecation).
		PCGEX_SHORTHAND_RENAME_PIN(CollectionPathAttributeName, AssetCollection, SourceCollection)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExAssetStagingSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 4)
	{
		// Collapse the old 3-way source: Asset -> Constant, Path Attribute -> Attribute. AttributeSet has no
		// equivalent -> empty Constant -> Boot errors with guidance.
		const EPCGExInputValueType NewInput = CollectionSource_DEPRECATED == EPCGExCollectionSource::Attribute
			                                      ? EPCGExInputValueType::Attribute
			                                      : EPCGExInputValueType::Constant;
		SourceCollection.Update(NewInput, CollectionPathAttributeName_DEPRECATED, AssetCollection_DEPRECATED.ToSoftObjectPath());
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

void UPCGExAssetStagingSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	EntryTypeFilter.PostEditChangeProperty(PropertyChangedEvent);
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TOptional<FPCGNodeThumbnailProxy> UPCGExAssetStagingSettings::GetNodeThumbnail() const
{
	if (SourceCollection.Input == EPCGExInputValueType::Constant && SourceCollection.Constant.IsValid())
	{
		return FPCGNodeThumbnailProxy::FromAssetPath(SourceCollection.Constant);
	}

	return {};
}
#endif

bool UPCGExAssetStagingSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExCollections::Labels::SourceSelectorLabel && SelectorMode == EPCGExSelectorMode::Legacy)
	{
		return false;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExAssetStagingSettings::WantsDataStealing() const
{
	return Super::WantsDataStealing() && !bPruneEmptyPoints;
}

PCGExData::EIOInit UPCGExAssetStagingSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_INITIALIZE_ELEMENT(AssetStaging)

PCGEX_ELEMENT_BATCH_POINT_IMPL(AssetStaging)

void UPCGExAssetStagingSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	if (SelectorMode != EPCGExSelectorMode::Legacy)
	{
		PCGEX_PIN_FACTORY(PCGExCollections::Labels::SourceSelectorLabel, "External selector factory driving entry picks.", Required, FPCGExDataTypeInfoSelector::AsId())
	}
	else
	{
		PCGEX_PIN_FACTORY(PCGExCollections::Labels::SourceSelectorLabel, "External selector factory driving entry picks.", Advanced, FPCGExDataTypeInfoSelector::AsId())
	}

	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExAssetStagingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		PCGEX_PIN_PARAM(PCGExCollections::Labels::OutputCollectionMapLabel, "Collection map generated by a staging node.", Normal)
	}

	if (bDoOutputSockets)
	{
		PCGEX_PIN_POINTS(PCGExStaging::Labels::OutputSocketLabel, "Socket points.", Normal)
	}
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExAssetStagingContext

void FPCGExAssetStagingContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	PCGEX_SETTINGS_LOCAL(AssetStaging)

	if (Settings->SourceCollection.Input == EPCGExInputValueType::Attribute && CollectionsLoader)
	{
		// Register the discovered collection paths for PCG to preload. Inner assets aren't needed -- Distribute
		// only emits paths/refs.
		CollectionsLoader->AddAssetDependencies();
	}
}

#pragma endregion

#pragma region FPCGExAssetStagingElement

bool FPCGExAssetStagingElement::Boot(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)

	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	Context->OutputMode = Settings->OutputMode;

	if (Settings->bOutputMaterialPicks)
	{
		PCGEX_VALIDATE_NAME(Settings->MaterialAttributePrefix)
		Context->bPickMaterials = true;
	}

	if (Settings->bWriteEntryType)
	{
		PCGEX_VALIDATE_NAME(Settings->EntryTypeAttributeName)
	}

	if (Settings->SelectorMode != EPCGExSelectorMode::Legacy)
	{
		TArray<TObjectPtr<const UPCGExSelectorFactoryData>> Factories;
		if (!PCGExFactories::GetInputFactories<UPCGExSelectorFactoryData>(Context, PCGExCollections::Labels::SourceSelectorLabel, Factories, {FPCGExDataTypeInfoSelector::AsId()}))
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("External distribution mode requires a Selector factory on the Selector input pin."));
			return false;
		}
		if (Factories.Num() != 1)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Exactly one Selector factory is expected on the Selector input pin."));
			return false;
		}
		Context->SelectorFactory = Factories[0];
	}
	else
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Legacy Distribution settings will be removed in the next update; make sure to update to 'External' (Detail Panel > Advanced > SelectorMode), and use a Selector : Classic."));
		Context->SelectorFactory = PCGExCollections::BuildLegacyFactory(Context, Settings->DistributionSettings);
	}

	if (!Context->SelectorFactory)
	{
		return Context->CancelExecution("Invalid Asset Selector");
	}

	if (Settings->SourceCollection.Input == EPCGExInputValueType::Attribute)
	{
		// Per-point / @Data mode: discover now (the loader is @Data/per-point aware); the framework loads the
		// collections before processing.
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->SourceCollection.Attribute)

		TArray<FName> Names = {Settings->SourceCollection.Attribute};
		Context->CollectionsLoader = MakeShared<PCGEx::TAssetLoader<UPCGExAssetCollection>>(Context, Context->MainPoints.ToSharedRef(), Names);

		if (!Context->CollectionsLoader->Discover())
		{
			return Context->CancelExecution(TEXT("Failed to find any collections to load."));
		}

		if (!Context->CollectionsLoader->Load())
		{
			return Context->CancelExecution(TEXT("Failed to load any collections."));
		}

		Context->CollectionsLoader->Finalize();

		// Cache lookups on each loaded collection.
		for (const TPair<PCGExValueHash, TObjectPtr<UPCGExAssetCollection>>& Pair : Context->CollectionsLoader->AssetsMap)
		{
			Pair.Value->LoadCache();
		}
	}
	else
	{
		// Constant mode: a single collection ref -- also resolves a Build Asset Collection node's transient
		// collection wired into the Constant override.
		const TSoftObjectPtr<UPCGExAssetCollection> CollectionRef(Settings->SourceCollection.Constant);
		PCGExHelpers::LoadBlocking_AnyThreadTpl(CollectionRef, Context);
		Context->MainCollection = CollectionRef.Get();
		if (!Context->MainCollection)
		{
			// Ex-AttributeSet nodes migrate to an empty Constant -- give targeted guidance.
			if (Settings->CollectionSource_DEPRECATED == EPCGExCollectionSource::AttributeSet)
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("The 'Attribute Set' collection source was removed from Staging : Distribute. Use a 'Build Asset Collection' node to convert your attribute set into a collection, then connect its output to this node's Source Collection (Constant) input."));
			}
			else
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("Missing asset collection."));
			}
			return false;
		}

		Context->MainCollection->EDITOR_RegisterTrackingKeys(Context);
	}


	if (Context->bPickMaterials && Context->MainCollection && !Context->MainCollection->IsType(PCGExAssetCollection::TypeIds::Mesh))
	{
		Context->bPickMaterials = false;
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Pick Material is enabled, but the selected collection doesn't support material picking."));
	}

	PCGEX_VALIDATE_NAME(Settings->AssetPathAttributeName)

	if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Raw || Settings->WeightToAttribute == EPCGExWeightOutputMode::Normalized)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->WeightAttributeName)
	}

	if (Context->OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		Context->CollectionPickDatasetPacker = MakeShared<PCGExCollections::FPickPacker>(Context);
	}

	Context->SelectorSharedDataCache = MakeShared<PCGExCollections::FSelectorSharedDataCache>();
	// Batch totals for AllInputs-scoped selector state (global budget + deterministic
	// per-input pre-split); must precede any GetOrBuild (see OnCached).
	Context->SelectorSharedDataCache->SetBatchPointCounts(*Context->MainPoints);

	if (Settings->bDoOutputSockets)
	{
		PCGEX_FWD(OutputSocketDetails)
		if (!Context->OutputSocketDetails.Init(Context))
		{
			return false;
		}

		Context->SocketsCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->SocketsCollection->OutputPin = PCGExStaging::Labels::OutputSocketLabel;
	}

	return true;
}

#if WITH_EDITOR
FString UPCGExAssetStagingSettings::GetDisplayName() const
{
	FString DisplayName;
	if (SourceCollection.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName = SourceCollection.Attribute.ToString();
	}
	else
	{
		DisplayName = SourceCollection.Constant.IsValid() ? SourceCollection.Constant.GetAssetName() : TEXT("None");
	}

	return TEXT("Staging [ ") + DisplayName + TEXT(" ]");
}
#endif

bool FPCGExAssetStagingElement::PostBoot(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)

	if (Settings->SourceCollection.Input == EPCGExInputValueType::Attribute)
	{
		if (Context->CollectionsLoader && Context->CollectionsLoader->IsEmpty())
		{
			return Context->CancelExecution(TEXT("Failed to load any collection from points."));
		}
	}
	else
	{
		check(Context->MainCollection)
		if (Context->MainCollection->LoadCache()->IsEmpty())
		{
			if (!Settings->bQuietEmptyCollectionError)
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Selected asset collection is empty."));
			}

			return false;
		}
	}

	return FPCGExPointsProcessorElement::PostBoot(InContext);
}

bool FPCGExAssetStagingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAssetStagingElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		const bool bAttributeMode = (Settings->SourceCollection.Input == EPCGExInputValueType::Attribute);

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				if (bAttributeMode)
				{
					NewBatch->bSkipCompletion = true;
				}
				else
				{
					NewBatch->bRequiresWriteStep = Settings->bPruneEmptyPoints;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	if (Context->OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
		Context->CollectionPickDatasetPacker->PackToDataset(OutputSet);

		FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
		OutData.Data = OutputSet;
	}

	if (Context->SocketsCollection)
	{
		Context->SocketsCollection->StageOutputs();
	}

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExAssetStaging::FProcessor

namespace PCGExAssetStaging
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExAssetStaging::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		bApplyFitting = Settings->bApplyFitting;
		NumPoints = PointDataFacade->GetNum();

		if (Context->bPickMaterials)
		{
			CachedPicks.Init(nullptr, NumPoints);
			MaterialPick.Init(-1, NumPoints);
		}

		if (bApplyFitting)
		{

			FittingHandler.ScaleToFit = Settings->ScaleToFit;
			FittingHandler.Justification = Settings->Justification;

			if (!FittingHandler.Init(ExecutionContext, PointDataFacade))
			{
				return false;
			}

			if (Settings->bWriteTranslation)
			{
				TranslationWriter = PointDataFacade->GetWritable<FVector>(Settings->TranslationAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::Inherit);
			}

			Variations = Settings->Variations;
			Variations.Init(Settings->Seed);

		}

		Source = MakeShared<PCGExCollections::FCollectionSource>(PointDataFacade);
		Source->DistributionSettings = Settings->DistributionSettings;
		Source->EntryDistributionSettings = Settings->EntryDistributionSettings;
		Source->SetSharedDataCache(Context->SelectorSharedDataCache);

		if (Settings->SourceCollection.Input == EPCGExInputValueType::Attribute)
		{
			if (!Source->Init(Context->CollectionsLoader->AssetsMap, Context->CollectionsLoader->GetKeys(PointDataFacade->Source->IOIndex), Context->SelectorFactory))
			{
				return false;
			}
		}
		else
		{
			if (!Source->Init(Context->MainCollection, Context->SelectorFactory))
			{
				return false;
			}
		}

		if (Settings->bDoOutputSockets)
		{
			SocketHelper = MakeShared<PCGExCollections::FSocketHelper>(&Context->OutputSocketDetails, NumPoints);
		}

		bOutputWeight = Settings->WeightToAttribute != EPCGExWeightOutputMode::NoOutput;
		bNormalizedWeight = Settings->WeightToAttribute != EPCGExWeightOutputMode::Raw;
		bOneMinusWeight = Settings->WeightToAttribute == EPCGExWeightOutputMode::NormalizedInverted || Settings->WeightToAttribute == EPCGExWeightOutputMode::NormalizedInvertedToDensity;

		if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Raw)
		{
			WeightWriter = PointDataFacade->GetWritable<int32>(Settings->WeightAttributeName, PCGExData::EBufferInit::New);
		}
		else if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Normalized)
		{
			NormalizedWeightWriter = PointDataFacade->GetWritable<double>(Settings->WeightAttributeName, PCGExData::EBufferInit::New);
		}

		if (Settings->bWriteEntryType)
		{
			EntryTypeWriter = PointDataFacade->GetWritable<FName>(Settings->EntryTypeAttributeName, NAME_None, true, PCGExData::EBufferInit::Inherit);
		}

		// bInherit: if the attribute already exists, preserve values for invalid points instead of clearing them
		if (Context->OutputMode == EPCGExStagingOutputMode::Attributes)
		{
			bInherit = PointDataFacade->GetIn()->Metadata->HasAttribute(Settings->AssetPathAttributeName);
			PathWriter = PointDataFacade->GetWritable<FSoftObjectPath>(Settings->AssetPathAttributeName, bInherit ? PCGExData::EBufferInit::Inherit : PCGExData::EBufferInit::New);
		}
		else
		{
			bInherit = PointDataFacade->GetIn()->Metadata->HasAttribute(PCGExCollections::Labels::Tag_EntryIdx);
			HashWriter = PointDataFacade->GetWritable<int64>(PCGExCollections::Labels::Tag_EntryIdx, bInherit ? PCGExData::EBufferInit::Inherit : PCGExData::EBufferInit::New);
		}

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		if (bApplyFitting)
		{
			AllocateFor |= EPCGPointNativeProperties::BoundsMin;
			AllocateFor |= EPCGPointNativeProperties::BoundsMax;
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}

		if (bOutputWeight && !WeightWriter && !NormalizedWeightWriter)
		{
			// No explicit weight attribute - fall back to writing weight into Density
			bUsesDensity = true;
			AllocateFor |= EPCGPointNativeProperties::Density;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		if (Settings->bPruneEmptyPoints)
		{
			Mask.Init(1, PointDataFacade->GetNum());
		}

		// Pre-register every collection (and its flat host set) with the packer before going
		// parallel. GetPickIdx is lock-free and assumes all reachable Hosts are mapped -- without
		// this call, PackToDataset would omit any Host that only surfaces via GetEntry recursion.
		if (Context->CollectionPickDatasetPacker)
		{
			Source->RegisterCollectionsTo(*Context->CollectionPickDatasetPacker);
		}

		// Pre-register + seal the socket helper: Add() in the parallel loop becomes lock-free.
		// Every leaf entry reachable via FlatHosts gets an FSocketInfos slot upfront (even ones
		// that never get picked) -- the trade-off is memory for contention-free parallel writes.
		if (SocketHelper)
		{
			Source->RegisterSocketsTo(*SocketHelper);
		}

		// Capacity-claiming selectors (Quota max caps) resolve every pick deterministically
		// before the main loop: parallel first-choice pass, then a sequential point-order
		// commit in OnRangeProcessingComplete. Zero cost when no such selector is connected.
		if (Source->AnyPickerWantsPreResolve())
		{
			Source->BeginPreResolve(NumPoints);
			RangeStage = ERangeStage::PreResolve;
			StartParallelLoopForRange(NumPoints);
		}
		else
		{
			StartParallelLoopForPoints();
		}

		return true;
	}

	void FProcessor::PreResolveScope(const PCGExMT::FScope& Scope, const bool bCommit)
	{
		if (!bCommit)
		{
			PointDataFacade->Fetch(Scope);
			FilterScope(Scope);
		}

		const TConstPCGValueRange<int32> Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();
		const UPCGComponent* Component = Context->GetComponent();
		const TSharedPtr<PCGExCollections::FSourceScratches> PickScratches = Source->CreateScratches(Scope.Count);

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExCollections::FSelectorHelper* Helper = nullptr;
			PCGExCollections::FMicroSelectorHelper* MicroHelper = nullptr;

			// Mirror the main loop's gating exactly -- a point that won't pick must not
			// influence quota capacity.
			if (!PointFilterCache[Index] || !Source->TryGetHelpers(Index, Helper, MicroHelper))
			{
				continue;
			}

			const int32 Seed = PCGExRandomHelpers::GetSeed(Seeds[Index], Helper->Details.SeedComponents, Helper->Details.LocalSeed, Settings, Component);
			const PCGExCollections::FSelectorScratches* Scratches = PickScratches ? PickScratches->GetFor(Helper) : nullptr;
			if (bCommit)
			{
				Helper->CommitPreResolve(Index, Seed, Scratches);
			}
			else
			{
				Helper->PreResolveFirstChoice(Index, Seed, Scratches);
			}
		}
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		// Thread-safe per-scope max tracking for material slot indices
		HighestSlotIndex = MakeShared<PCGExMT::TScopedNumericValue<int8>>(Loops, -1);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::AssetStaging::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		if (!bFiltersPrimed)
		{
			FilterScope(Scope);
		}

		const bool bLocalApplyFitting = bApplyFitting;
		const bool bLocalOutputWeight = bOutputWeight;
		const bool bFlattenSubCollections = Settings->bFlattenSubCollections;
		const bool bConsiderEntryScaleToFit = Settings->bConsiderEntryScaleToFit;
		const bool bConsiderEntryJustification = Settings->bConsiderEntryJustification;
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		const TPCGValueRange<FTransform> OutTransforms = bLocalApplyFitting ? OutPointData->GetTransformValueRange(false) : TPCGValueRange<FTransform>();
		const TPCGValueRange<FVector> OutBoundsMin = bLocalApplyFitting ? OutPointData->GetBoundsMinValueRange(false) : TPCGValueRange<FVector>();
		const TPCGValueRange<FVector> OutBoundsMax = bLocalApplyFitting ? OutPointData->GetBoundsMaxValueRange(false) : TPCGValueRange<FVector>();
		const TConstPCGValueRange<int32> Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();
		const TPCGValueRange<float> Densities = bUsesDensity ? OutPointData->GetDensityValueRange(false) : TPCGValueRange<float>();

		int32 LocalNumInvalid = 0;

		// Handles points that fail distribution or filtering.
		// When bInherit is true, we preserve existing attribute values (from previous staging pass).
		// Otherwise we either mark for pruning or write sentinel values.
		auto InvalidPoint = [&](const int32 Index)
		{
			if (bInherit)
			{
				return;
			}

			// Keep existing values from upstream staging

			if (Settings->bPruneEmptyPoints)
			{
				Mask[Index] = 0;
				LocalNumInvalid++;
				return;
			}

			// Write sentinel values so downstream nodes can detect unstaged points
			if (PathWriter)
			{
				PathWriter->SetValue(Index, FSoftObjectPath{});
			}
			else
			{
				HashWriter->SetValue(Index, -1);
			}

			if (bLocalOutputWeight)
			{
				if (WeightWriter)
				{
					WeightWriter->SetValue(Index, -1);
				}
				else if (NormalizedWeightWriter)
				{
					NormalizedWeightWriter->SetValue(Index, -1);
				}
			}

			if (Context->bPickMaterials)
			{
				MaterialPick[Index] = -1;
			}
		};

		const UPCGComponent* Component = Context->GetComponent();
		int32 LocalHighestSlotIndex = 0;
		FRandomStream RandomSource;

		const bool bFilterEntryType = Settings->bDoFilterEntryType;
		const FPCGExStagedTypeFilterDetails& EntryTypeFilter = Settings->EntryTypeFilter;

		// Per-scope pick scratches -- this scope runs on a single thread, so ops can mutate
		// their scratch freely. Null when no active selector op uses scratch.
		const TSharedPtr<PCGExCollections::FSourceScratches> PickScratches = Source->CreateScratches(Scope.Count);

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExCollections::FSelectorHelper* Helper = nullptr;
			PCGExCollections::FMicroSelectorHelper* MicroHelper = nullptr;

			if (!PointFilterCache[Index] || !Source->TryGetHelpers(Index, Helper, MicroHelper))
			{
				InvalidPoint(Index);
				continue;
			}

			const int32 Seed = PCGExRandomHelpers::GetSeed(Seeds[Index], Helper->Details.SeedComponents, Helper->Details.LocalSeed, Settings, Component);

			FPCGExEntryAccessResult Result = Helper->GetEntry(Index, Seed, bFlattenSubCollections, PickScratches ? PickScratches->GetFor(Helper) : nullptr);

			if (!Result.IsValid()
				|| !Result.Entry->Staging.Bounds.IsValid
				|| (bFilterEntryType && !EntryTypeFilter.Matches(Result.Entry->GetTypeId())))
			{
				InvalidPoint(Index);
				continue;
			}

			const FPCGExAssetCollectionEntry* Entry = Result.Entry;
			const UPCGExAssetCollection* EntryHost = Result.Host;

			int16 SecondaryIndex = -1; // Material variant index within the entry

			const FPCGExAssetStagingData& Staging = Entry->Staging;

			// MicroCache holds per-entry sub-distribution data (e.g., material variants for meshes).
			// SecondaryIndex selects which variant to use for this point.
			if (const PCGExAssetCollection::FMicroCache* MicroCache = Entry->MicroCache.Get();
				MicroHelper && MicroCache && MicroCache->GetTypeId() == PCGExAssetCollection::TypeIds::Mesh)
			{
				const PCGExMeshCollection::FMicroCache* EntryMicroCache = static_cast<const PCGExMeshCollection::FMicroCache*>(MicroCache);
				SecondaryIndex = MicroHelper->GetPick(EntryMicroCache, Index, PCGExRandomHelpers::GetSeed(Seed, Index));

				if (Context->bPickMaterials)
				{
					MaterialPick[Index] = SecondaryIndex;
					CachedPicks[Index] = Entry;
					// Track highest slot index seen so we know how many material attributes to create
					LocalHighestSlotIndex = FMath::Max(LocalHighestSlotIndex, EntryMicroCache->GetHighestIndex());
				}
			}
			else if (Context->bPickMaterials)
			{
				MaterialPick[Index] = -1;
			}

			if (bLocalOutputWeight)
			{
				double Weight = bNormalizedWeight ? static_cast<double>(Entry->Weight) / static_cast<double>(const_cast<UPCGExAssetCollection*>(EntryHost)->LoadCache()->WeightSum) : Entry->Weight;
				if (bOneMinusWeight)
				{
					Weight = 1 - Weight;
				}
				if (WeightWriter)
				{
					WeightWriter->SetValue(Index, Weight);
				}
				else if (NormalizedWeightWriter)
				{
					NormalizedWeightWriter->SetValue(Index, Weight);
				}
				else
				{
					Densities[Index] = Weight;
				}
			}

			if (PathWriter)
			{
				PathWriter->SetValue(Index, Staging.Path);
			}
			else
			{
				HashWriter->SetValue(Index, Context->CollectionPickDatasetPacker->GetPickIdx(EntryHost, Staging.InternalIndex, SecondaryIndex));
			}

			if (bLocalApplyFitting)
			{

				FTransform& OutTransform = OutTransforms[Index];
				FVector OutTranslation = FVector::ZeroVector;
				FBox OutBounds = Entry->Staging.AlteredBounds;

				const FPCGExFittingVariations& EntryVariations = Entry->GetVariations(EntryHost);

				PCGExFitting::FOverridesView EntryOverrides;
				if (bConsiderEntryScaleToFit)
				{
					EntryOverrides.ScaleToFit = Entry->GetScaleToFitOverride(EntryHost);
				}
				if (bConsiderEntryJustification)
				{
					EntryOverrides.Justification = Entry->GetJustificationOverride(EntryHost);
				}

				RandomSource.Initialize(PCGExRandomHelpers::GetSeed(Seed, Variations.Seed));

				// "Before" variations modify asset bounds before fitting, affecting scale-to-fit calculation.
				// "After" variations apply to the final transform without changing bounds.
				if (Variations.bEnabledBefore)
				{
					FTransform LocalXForm = FTransform::Identity;
					Variations.Apply(RandomSource, LocalXForm, EntryVariations, EPCGExVariationMode::Before);
					FittingHandler.ComputeLocalTransform(Index, LocalXForm, OutTransform, OutBounds, OutTranslation, EntryOverrides);
				}
				else
				{
					FittingHandler.ComputeTransform(Index, OutTransform, OutBounds, OutTranslation, true, EntryOverrides);
				}

				if (TranslationWriter)
				{
					TranslationWriter->SetValue(Index, OutTranslation);
				}

				OutBoundsMin[Index] = OutBounds.Min;
				OutBoundsMax[Index] = OutBounds.Max;

				if (Variations.bEnabledAfter)
				{
					Variations.Apply(RandomSource, OutTransform, EntryVariations, EPCGExVariationMode::After);
				}
			}

			if (SocketHelper)
			{
				// Hash scheme must match FSocketHelper::RegisterCollection (and LoadSockets' GetSimplifiedEntryHash).
				const uint64 EntryHash = PCGEx::H64(EntryHost->GetCollectionGUID(), Staging.InternalIndex);
				SocketHelper->Add(Index, EntryHash, Entry);
			}

			if (EntryTypeWriter)
			{
				EntryTypeWriter->SetValue(Index, Result.Entry->GetTypeId());
			}
		}

		// Merge per-scope max into shared tracker (thread-safe)
		HighestSlotIndex->Set(Scope, FMath::Max(LocalHighestSlotIndex, HighestSlotIndex->Get(Scope)));
		FPlatformAtomics::InterlockedAdd(&NumInvalid, LocalNumInvalid);
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (SocketHelper)
		{
			SocketHelper->Compile(TaskManager, PointDataFacade, Context->SocketsCollection);
		}

		if (Context->bPickMaterials)
		{
			// Create one attribute per material slot used across all picked entries.
			// HighestSlotIndex was tracked during ProcessPoints to know how many we need.
			int8 WriterCount = HighestSlotIndex->Max() + 1;
			if (Settings->MaxMaterialPicks > 0)
			{
				WriterCount = Settings->MaxMaterialPicks;
			}

			if (WriterCount > 0)
			{
				MaterialWriters.Init(nullptr, WriterCount);

				for (int i = 0; i < WriterCount; i++)
				{
					const FName AttributeName = FName(FString::Printf(TEXT("%s_%d"), *Settings->MaterialAttributePrefix.ToString(), i));
					MaterialWriters[i] = PointDataFacade->GetWritable<FSoftObjectPath>(AttributeName, FSoftObjectPath(), true, PCGExData::EBufferInit::New);
				}

				// Second parallel pass to write material picks (separate from main loop for buffer allocation)
				StartParallelLoopForRange(NumPoints);
				return;
			}

			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No material were picked -- no attribute will be written."));
		}

		OnRangeProcessingComplete();
	}

	// PreResolve stage: selector first-choice pass. Materials stage: writes material override
	// paths to per-slot attributes based on the picks made in ProcessPoints.
	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		if (RangeStage == ERangeStage::PreResolve)
		{
			PreResolveScope(Scope, false);
			return;
		}

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 Pick = MaterialPick[Index];

			if (Pick == -1 || (Settings->bPruneEmptyPoints && !Mask[Index]))
			{
				continue;
			}

			const FPCGExMeshCollectionEntry* Entry = static_cast<const FPCGExMeshCollectionEntry*>(CachedPicks[Index]);
			if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::None)
			{
				continue;
			}

			// Single mode: one material slot override
			if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::Single)
			{
				if (!MaterialWriters.IsValidIndex(Entry->SlotIndex))
				{
					continue;
				}
				MaterialWriters[Entry->SlotIndex]->SetValue(Index, Entry->MaterialOverrideVariants[Pick].Material.ToSoftObjectPath());
			}
			// Multi mode: multiple material slot overrides per variant
			else if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::Multi)
			{
				const FPCGExMaterialOverrideCollection& MEntry = Entry->MaterialOverrideVariantsList[Pick];

				for (int i = 0; i < MEntry.Overrides.Num(); i++)
				{
					const FPCGExMaterialOverrideEntry& SlotEntry = MEntry.Overrides[i];

					const int32 SlotIndex = SlotEntry.SlotIndex == -1 ? 0 : SlotEntry.SlotIndex;
					if (!MaterialWriters.IsValidIndex(SlotIndex))
					{
						continue;
					}
					MaterialWriters[SlotIndex]->SetValue(Index, SlotEntry.Material.ToSoftObjectPath());
				}
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		if (RangeStage == ERangeStage::PreResolve)
		{
			// Sequential commit in ascending point-index order -- this ordering IS the
			// deterministic claim priority for capped entries.
			PreResolveScope(PCGExMT::FScope(0, NumPoints), true);

			// The pre-pass filled the full filter cache; the main loop skips re-evaluation.
			bFiltersPrimed = true;
			RangeStage = ERangeStage::Materials;
			StartParallelLoopForPoints();
			return;
		}

		PointDataFacade->WriteBuffers(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->Write();
			});
	}

	void FProcessor::Write()
	{
		if (Settings->bPruneEmptyPoints)
		{
			// Release Source before Gather since Gather will invalidate indices it references
			Source.Reset();
			(void)PointDataFacade->Source->Gather(Mask);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
