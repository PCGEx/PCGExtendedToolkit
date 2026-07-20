// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingTypeFilter.h"

#include "PCGParamData.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExBucketDispatchHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExStagedTypeFilterElement"
#define PCGEX_NAMESPACE StagedTypeFilter

#pragma region FPCGExStagedTypeFilterConfig

#pragma endregion

#pragma region UPCGSettings

#if WITH_EDITOR
void UPCGExStagedTypeFilterSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	TypeConfig.PostEditChangeProperty(PropertyChangedEvent);

	// Rebuild TypePinLabels from enabled types
	TypePinLabels.Reset();
	if (FilterMode == EPCGExStagedTypeFilterMode::PinPerType)
	{
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([this](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.Id == PCGExAssetCollection::TypeIds::Base || Info.Id == PCGExAssetCollection::TypeIds::None)
			{
				return;
			}

			const bool* Value = TypeConfig.TypeFilter.Find(Info.Id);
			if (Value && *Value)
			{
				TypePinLabels.Add(Info.Id);
			}
		});
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

TArray<FPCGPinProperties> UPCGExStagedTypeFilterSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from staging nodes.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExStagedTypeFilterSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (FilterMode == EPCGExStagedTypeFilterMode::PinPerType)
	{
		// Discarded pin first (index 0)
		if (bOutputDiscarded)
		{
			PCGEX_PIN_POINTS(PCGExStagedTypeFilter::OutputFilteredOut, "Points that didn't match any enabled type.", Normal)
		}

		// Per-type pins
		for (const FName& Label : TypePinLabels)
		{
			PCGEX_PIN_POINTS(Label, "Points matching this collection type.", Normal)
		}
	}
	else if (FilterMode == EPCGExStagedTypeFilterMode::EntryShape)
	{
		// Discarded pin first (index 0)
		if (bOutputDiscarded)
		{
			PCGEX_PIN_POINTS(PCGExStagedTypeFilter::OutputFilteredOut, "Points whose entry could not be resolved (or invalid collections when discard is enabled).", Normal)
		}

		PCGEX_PIN_POINTS(PCGExStagedTypeFilter::OutputLeaf, "Points whose entry is a direct asset (not a subcollection).", Normal)
		PCGEX_PIN_POINTS(PCGExStagedTypeFilter::OutputCollection, "Points whose entry is a subcollection.", Normal)
	}
	else
	{
		// Include/Exclude modes - default output from Super + optional Discarded
		PinProperties = Super::OutputPinProperties();

		if (bOutputDiscarded)
		{
			PCGEX_PIN_POINTS(PCGExStagedTypeFilter::OutputFilteredOut, "Points that were filtered out.", Normal)
		}
	}

	return PinProperties;
}

FName UPCGExStagedTypeFilterSettings::GetMainOutputPin() const
{
	if (IsBucketMode())
	{
		return PCGExStagedTypeFilter::OutputFilteredOut;
	}
	return Super::GetMainOutputPin();
}

#pragma endregion

#pragma region FPCGExStagedTypeFilterContext

int32 FPCGExStagedTypeFilterContext::FindTypeBucket(PCGExAssetCollection::FTypeId TypeId) const
{
	if (TypeId == PCGExAssetCollection::TypeIds::None || TypeId == PCGExAssetCollection::TypeIds::Base)
	{
		return -1;
	}

	const int32* Bucket = TypeToBucketMap.Find(TypeId);
	if (Bucket)
	{
		return *Bucket;
	}

	// Walk parent chain
	const PCGExAssetCollection::FTypeId ParentType = PCGExAssetCollection::FTypeRegistry::Get().GetParentType(TypeId);
	if (ParentType != PCGExAssetCollection::TypeIds::None)
	{
		return FindTypeBucket(ParentType);
	}

	return -1;
}

#pragma endregion

PCGEX_INITIALIZE_ELEMENT(StagedTypeFilter)
PCGEX_ELEMENT_BATCH_POINT_IMPL(StagedTypeFilter)

#pragma region FPCGExStagedTypeFilterElement

bool FPCGExStagedTypeFilterElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagedTypeFilter)

	// Setup collection unpacker
	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	if (Settings->FilterMode == EPCGExStagedTypeFilterMode::PinPerType)
	{
		// Build TypeToBucketMap from enabled types
		int32 BucketIndex = 0;
		for (const FName& Label : Settings->TypePinLabels)
		{
			Context->TypeToBucketMap.Add(Label, BucketIndex);
			BucketIndex++;
		}

		if (BucketIndex == 0)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("No types enabled in PinPerType mode. All points will be discarded."));
		}

		// Create per-type output collections
		Context->TypeOutputs.SetNum(BucketIndex);
		for (int32 i = 0; i < BucketIndex; i++)
		{
			Context->TypeOutputs[i] = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->TypeOutputs[i]->OutputPin = Settings->TypePinLabels[i];
		}

		// Create unmatched output
		if (Settings->bOutputDiscarded)
		{
			Context->UnmatchedOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->UnmatchedOutput->OutputPin = PCGExStagedTypeFilter::OutputFilteredOut;
		}
	}
	else if (Settings->FilterMode == EPCGExStagedTypeFilterMode::EntryShape)
	{
		// Two fixed buckets: 0 = Leaf, 1 = Collection
		Context->TypeOutputs.SetNum(2);
		Context->TypeOutputs[0] = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->TypeOutputs[0]->OutputPin = PCGExStagedTypeFilter::OutputLeaf;
		Context->TypeOutputs[1] = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->TypeOutputs[1]->OutputPin = PCGExStagedTypeFilter::OutputCollection;

		// Create unmatched output
		if (Settings->bOutputDiscarded)
		{
			Context->UnmatchedOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->UnmatchedOutput->OutputPin = PCGExStagedTypeFilter::OutputFilteredOut;
		}
	}
	else
	{
		// Include/Exclude mode
		if (Settings->bOutputDiscarded)
		{
			Context->FilteredOutCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->FilteredOutCollection->OutputPin = PCGExStagedTypeFilter::OutputFilteredOut;
		}
	}

	return true;
}

bool FPCGExStagedTypeFilterElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagedTypeFilterElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagedTypeFilter)
	PCGEX_EXECUTION_CHECK

	if (Settings->IsBucketMode())
	{
		PCGEX_ON_INITIAL_EXECUTION
		{
			Context->NumPairs = Context->MainPoints->Pairs.Num();

			// Pre-size all bucket collections
			for (int32 i = 0; i < Context->TypeOutputs.Num(); i++)
			{
				Context->TypeOutputs[i]->Pairs.Init(nullptr, Context->NumPairs);
			}

			if (Context->UnmatchedOutput)
			{
				Context->UnmatchedOutput->Pairs.Init(nullptr, Context->NumPairs);
			}

			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
					NewBatch->bSkipCompletion = true;
				}))
			{
				return Context->CancelExecution(TEXT("Could not find any points to process."));
			}
		}

		PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

		// Prune null entries
		for (int32 i = 0; i < Context->TypeOutputs.Num(); i++)
		{
			Context->TypeOutputs[i]->PruneNullEntries(true);
		}

		if (Context->UnmatchedOutput)
		{
			Context->UnmatchedOutput->PruneNullEntries(true);
		}

		// Stage outputs and build inactive pin bitmask
		uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;
		int32 PinIndex = 0;

		if (Settings->bOutputDiscarded)
		{
			if (!Context->UnmatchedOutput->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}

		for (int32 i = 0; i < Context->TypeOutputs.Num(); i++)
		{
			if (!Context->TypeOutputs[i]->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}

		return Context->TryComplete();
	}
	// Include/Exclude mode (unchanged)
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	if (Context->FilteredOutCollection)
	{
		Context->FilteredOutCollection->StageOutputs();
	}

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExStagedTypeFilter::FProcessor

namespace PCGExStagedTypeFilter
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagedTypeFilter::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->IsBucketMode())
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::NoInit)
		}
		else
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		}

		// Get hash attribute -- silently skip data without staging hashes
		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			return false;
		}

		if (!Settings->IsBucketMode())
		{
			// Initialize mask for Include/Exclude
			Mask.Init(1, PointDataFacade->GetNum());
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		if (!Settings->IsBucketMode())
		{
			return;
		}

		const int32 NumBuckets = Context->TypeOutputs.Num();
		const int32 TotalBuckets = NumBuckets + 1; // +1 for unmatched
		const int32 MaxRange = PCGExMT::FScope::GetMaxRange(Loops);

		BucketIndices.SetNum(TotalBuckets);
		BucketCounts.Init(0, TotalBuckets);

		for (int32 i = 0; i < TotalBuckets; i++)
		{
			BucketIndices[i] = MakeShared<PCGExMT::TScopedArray<int32>>(Loops);
			BucketIndices[i]->Reserve(MaxRange);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagedTypeFilter::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		if (Settings->IsBucketMode())
		{
			const int32 UnmatchedIdx = Context->TypeOutputs.Num();
			const bool bIsEntryShape = Settings->FilterMode == EPCGExStagedTypeFilterMode::EntryShape;
			const bool bDiscardInvalidCollections = Settings->bDiscardInvalidCollections;

			PCGEX_SCOPE_LOOP(Index)
			{
				const uint64 Hash = EntryHashGetter->Read(Index);
				int32 Bucket = -1;

				if (Hash != 0 && Hash != static_cast<uint64>(-1))
				{
					int16 SecondaryIndex = 0;
					FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, SecondaryIndex);

					if (Result.IsValid())
					{
						if (bIsEntryShape)
						{
							if (!Result.Entry->bIsSubCollection)
							{
								Bucket = 0; // Leaf
							}
							else if (!bDiscardInvalidCollections || Result.Entry->GetSubCollectionPtr())
							{
								Bucket = 1; // Collection
							}
							// else: invalid subcollection -- fall through to UnmatchedIdx
						}
						else
						{
							Bucket = Context->FindTypeBucket(Result.Entry->GetTypeId());
						}
					}
				}

				if (Bucket >= 0)
				{
					BucketIndices[Bucket]->Get_Ref(Scope).Add(Index);
					FPlatformAtomics::InterlockedAdd(&BucketCounts[Bucket], 1);
				}
				else
				{
					BucketIndices[UnmatchedIdx]->Get_Ref(Scope).Add(Index);
					FPlatformAtomics::InterlockedAdd(&BucketCounts[UnmatchedIdx], 1);
				}
			}
		}
		else
		{
			// Include/Exclude mode
			const bool bIncludeMode = Settings->FilterMode == EPCGExStagedTypeFilterMode::Include;
			int32 LocalKept = 0;

			PCGEX_SCOPE_LOOP(Index)
			{
				const uint64 Hash = EntryHashGetter->Read(Index);

				PCGExAssetCollection::FTypeId TypeId = PCGExAssetCollection::TypeIds::None;

				if (Hash != 0 && Hash != static_cast<uint64>(-1))
				{
					int16 SecondaryIndex = 0;
					FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, SecondaryIndex);

					if (Result.IsValid())
					{
						TypeId = Result.Entry->GetTypeId();
					}
				}

				const bool bMatchesConfig = Settings->TypeConfig.Matches(TypeId);

				// In Include mode: keep if matches. In Exclude mode: keep if doesn't match.
				const bool bKeep = bIncludeMode ? bMatchesConfig : !bMatchesConfig;

				if (bKeep)
				{
					Mask[Index] = 1;
					LocalKept++;
				}
				else
				{
					Mask[Index] = 0;
				}
			}

			FPlatformAtomics::InterlockedAdd(&NumKept, LocalKept);
		}
	}

	TSharedPtr<PCGExData::FPointIO> FProcessor::CreateIO(const TSharedRef<PCGExData::FPointIOCollection>& InCollection, const PCGExData::EIOInit InitMode) const
	{
		TSharedPtr<PCGExData::FPointIO> NewPointIO = PCGExData::NewPointIO(PointDataFacade->Source, InCollection->OutputPin);

		if (!NewPointIO->InitializeOutput(InitMode))
		{
			return nullptr;
		}

		InCollection->Pairs[BatchIndex] = NewPointIO;
		return NewPointIO;
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (!Settings->IsBucketMode())
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagedTypeFilter::OnPointsProcessingComplete);

		PCGExBucketDispatchHelpers::DispatchBuckets(
			Context->TypeOutputs,
			Context->UnmatchedOutput,
			BucketCounts,
			BucketIndices,
			PointDataFacade->GetNum(),
			[this](const TSharedRef<PCGExData::FPointIOCollection>& InCollection, PCGExData::EIOInit InitMode)
			{
				return CreateIO(InCollection, InitMode);
			});
	}

	void FProcessor::CompleteWork()
	{
		// Bucket modes (PinPerType, EntryShape) are handled entirely by OnPointsProcessingComplete
		if (Settings->IsBucketMode())
		{
			return;
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		const int32 NumFiltered = NumPoints - NumKept;

		if (NumFiltered == 0)
		{
			// All points kept, nothing to do
			return;
		}

		if (NumKept == 0)
		{
			// All points filtered out
			if (Settings->bOutputDiscarded && Context->FilteredOutCollection)
			{
				// Move entire dataset to filtered out
				TSharedPtr<PCGExData::FPointIO> FilteredIO = Context->FilteredOutCollection->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::Forward);
			}

			// Clear main output
			PointDataFacade->Source->Disable();
			return;
		}

		// Output filtered out points if requested
		if (Settings->bOutputDiscarded && Context->FilteredOutCollection)
		{
			// Create inverted mask for filtered out points
			TArray<int8> InvertedMask;
			InvertedMask.SetNum(NumPoints);
			for (int32 i = 0; i < NumPoints; i++)
			{
				InvertedMask[i] = Mask[i] ? 0 : 1;
			}

			TSharedPtr<PCGExData::FPointIO> FilteredIO = Context->FilteredOutCollection->Emplace_GetRef(PointDataFacade->GetIn(), PCGExData::EIOInit::Duplicate);
			if (FilteredIO)
			{
				(void)FilteredIO->Gather(InvertedMask);
			}
		}

		// Gather kept points
		(void)PointDataFacade->Source->Gather(Mask);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
