// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSelfPruning.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/OBB/PCGExOBBTests.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

#define LOCTEXT_NAMESPACE "PCGExSelfPruningElement"
#define PCGEX_NAMESPACE SelfPruning

#if WITH_EDITOR
void UPCGExSelfPruningSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		// Rewire Primary Expansion
		PCGEX_SHORTHAND_RENAME_PIN(PrimaryExpansionAttribute, PrimaryExpansion, PrimaryExpansionValue)

		// Rewire Secondary Expansion
		PCGEX_SHORTHAND_RENAME_PIN(SecondaryExpansionAttribute, SecondaryExpansion, SecondaryExpansionValue)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSelfPruningSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		PrimaryExpansionValue.Update(PrimaryExpansionInput_DEPRECATED, PrimaryExpansionAttribute_DEPRECATED, PrimaryExpansion_DEPRECATED);
		SecondaryExpansionValue.Update(SecondaryExpansionInput_DEPRECATED, SecondaryExpansionAttribute_DEPRECATED, SecondaryExpansion_DEPRECATED);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool UPCGExSelfPruningSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (Mode != EPCGExSelfPruningMode::Prune && InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return false;
	}

	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExSelfPruningSettings::HasDynamicPins() const
{
	return IsInputless();
}

TArray<FPCGPinProperties> UPCGExSelfPruningSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Normal);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSelfPruningSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (bOutputDiscard && Mode == EPCGExSelfPruningMode::Prune)
	{
		PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Points that were pruned away.", Normal)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SelfPruning)
PCGEX_ELEMENT_BATCH_POINT_IMPL(SelfPruning)

bool FPCGExSelfPruningElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SelfPruning)

	PCGEX_VALIDATE_NAME_CONDITIONAL(Settings->Mode == EPCGExSelfPruningMode::WriteResult, Settings->NumOverlapAttributeName)

	if (Settings->bOutputDiscard && Settings->Mode == EPCGExSelfPruningMode::Prune)
	{
		Context->Discarded = MakeShared<PCGExData::FPointIOCollection>(InContext);
		Context->Discarded->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;
	}

	return true;
}

bool FPCGExSelfPruningElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSelfPruningElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SelfPruning)
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

	if (Context->Discarded)
	{
		// Bit 1 == the 'Discarded' pin (bit 0 is the main output). Deactivate it when no points were pruned.
		if (!Context->Discarded->StageOutputs())
		{
			Context->OutputData.InactiveOutputPinBitmask |= 1ULL << 1;
		}
	}

	Context->Done();

	return Context->TryComplete();
}


namespace PCGExSelfPruning
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSelfPruning::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->PrimaryMode != EPCGExSelfPruningExpandOrder::None)
		{
			PrimaryExpansion = Settings->PrimaryExpansionValue.GetValueSetting();
			if (!PrimaryExpansion->Init(PointDataFacade))
			{
				return false;
			}
		}

		if (Settings->SecondaryMode != EPCGExSelfPruningExpandOrder::None)
		{
			SecondaryExpansion = Settings->SecondaryExpansionValue.GetValueSetting();
			if (!SecondaryExpansion->Init(PointDataFacade))
			{
				return false;
			}
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		if (Settings->Mode == EPCGExSelfPruningMode::WriteResult)
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		}
		else
		{
			Mask.Init(true, NumPoints);
		}

		TArray<int32> Order;
		PCGExArrayHelpers::ArrayOfIndices(Order, NumPoints);

		PCGExArrayHelpers::InitArray(Candidates, NumPoints);
		Priority.SetNumUninitialized(NumPoints);
		BoxSecondary.Init(FBox(NoInit), NumPoints);

		// Allocate OBB arrays only when precise testing is enabled
		if (Settings->bPreciseTest)
		{
			PrimaryOBBs.SetNumUninitialized(NumPoints);
			SecondaryOBBs.SetNumUninitialized(NumPoints);
		}

		if (Settings->Mode == EPCGExSelfPruningMode::Prune)
		{
			TSharedPtr<PCGExSorting::FSorter> Sorter = MakeShared<PCGExSorting::FSorter>(Context, PointDataFacade, PCGExSorting::GetSortingRules(Context, PCGExSorting::Labels::SourceSortingRules));
			Sorter->SortDirection = Settings->SortDirection;

			PCGEX_SHARED_CONTEXT(Context->GetWeakSelfHandle())

			// When the sorting rules produce an ordering, SortDirection is already baked into Order;
			// the randomize pass below must then NOT re-apply it. With no rules, Order stays identity
			// and the randomize pass owns the SortDirection semantics.
			const bool bHasRuleOrder = Sorter->Init(Context);
			if (bHasRuleOrder)
			{
				if (TSharedPtr<PCGExSorting::FSortCache> Cache = Sorter->BuildCache(NumPoints))
				{
					Order.Sort([&](const int32 A, const int32 B)
					{
						return Cache->Compare(A, B);
					});
				}
				else
				{
					Order.Sort([&](const int32 A, const int32 B)
					{
						return Sorter->Sort(A, B);
					});
				}
			}

			if (Settings->bRandomize)
			{
				TConstPCGValueRange<int32> Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();
				const int32 MaxRange = NumPoints * Settings->RandomRange;
				const int32 MinRange = -MaxRange;

				// Perturb each point's rank (its position in Order) by a per-point random offset.
				// Priority is keyed by point index so the comparator below reads consistent values,
				// and the noise draws from that point's own seed (Order[i] is the point at rank i).
				for (int32 i = 0; i < NumPoints; i++)
				{
					const int32 PointIndex = Order[i];
					Priority[PointIndex] = i + PCGExRandomHelpers::GetRandomStreamFromPoint(Seeds[PointIndex], 0, Settings).RandRange(MinRange, MaxRange);
				}

				if (!bHasRuleOrder && Settings->SortDirection == EPCGExSortDirection::Descending)
				{
					// No rule ordering to honor: SortDirection flips the (otherwise identity) rank order.
					Order.Sort([&](const int32 A, const int32 B)
					{
						return Priority[A] > Priority[B];
					});
				}
				else
				{
					Order.Sort([&](const int32 A, const int32 B)
					{
						return Priority[A] < Priority[B];
					});
				}
			}
		}

		for (int32 i = 0; i < NumPoints; i++)
		{
			Priority[Order[i]] = i;
		}

		// Only force single-threaded for Prune mode (Mask is shared state)
		// WriteResult mode can run in parallel since each candidate's overlap count is independent
		bForceSingleThreadedProcessRange = (Settings->Mode == EPCGExSelfPruningMode::Prune);
		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		const UPCGBasePointData* InData = PointDataFacade->GetIn();
		TConstPCGValueRange<FTransform> Transforms = InData->GetConstTransformValueRange();

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			FCandidateInfos& Candidate = Candidates[Index];
			Candidate.Index = Index;
			Candidate.bSkip = false;
			Candidate.Overlaps = 0;
		}

		// Build BoxSecondary (world AABBs for octree pre-filtering)
		switch (Settings->SecondaryMode)
		{
		case EPCGExSelfPruningExpandOrder::Before:
			PCGEX_SCOPE_LOOP(Index)
			{
				BoxSecondary[Index] = InData->GetLocalBounds(Index).ExpandBy(SecondaryExpansion->Read(Index)).TransformBy(Transforms[Index]);
			}
			break;
		case EPCGExSelfPruningExpandOrder::After:
			PCGEX_SCOPE_LOOP(Index)
			{
				BoxSecondary[Index] = InData->GetLocalBounds(Index).TransformBy(Transforms[Index]).ExpandBy(SecondaryExpansion->Read(Index));
			}
			break;
		default:
		case EPCGExSelfPruningExpandOrder::None:
			PCGEX_SCOPE_LOOP(Index)
			{
				BoxSecondary[Index] = InData->GetLocalBounds(Index).TransformBy(Transforms[Index]);
			}
			break;
		}

		// Build pre-computed OBBs for precise testing
		if (Settings->bPreciseTest)
		{
			auto GetLocalBounds = [&](const int32 i, const FTransform& T)
			{
				const FBox LocalBounds = InData->GetLocalBounds(i);
				const FVector Scale = T.GetScale3D();
				return FBox(LocalBounds.Min * Scale, LocalBounds.Max * Scale);
			};

			// Build Secondary OBBs
			// Note: Both Before and After modes expand the local box for the OBB test
			// (the Before/After distinction only affects the world AABB in BoxSecondary)
			if (Settings->SecondaryMode == EPCGExSelfPruningExpandOrder::None)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					const FTransform& T = Transforms[Index];
					SecondaryOBBs[Index] = PCGExMath::OBB::Factory::FromTransform(
						T,
						GetLocalBounds(Index, T),
						Index);
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					const FTransform& T = Transforms[Index];
					SecondaryOBBs[Index] = PCGExMath::OBB::Factory::FromTransform(
						T,
						GetLocalBounds(Index, T).ExpandBy(SecondaryExpansion->Read(Index)),
						Index);
				}
			}

			// Build Primary OBBs
			// Note: Both Before and After modes expand the local box for the OBB test
			if (Settings->PrimaryMode == EPCGExSelfPruningExpandOrder::None)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					const FTransform& T = Transforms[Index];
					PrimaryOBBs[Index] = PCGExMath::OBB::Factory::FromTransform(
						T,
						GetLocalBounds(Index, T),
						Index);
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					const FTransform& T = Transforms[Index];
					PrimaryOBBs[Index] = PCGExMath::OBB::Factory::FromTransform(
						T,
						GetLocalBounds(Index, T).ExpandBy(PrimaryExpansion->Read(Index)),
						Index);
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		Candidates.Sort([&](const FCandidateInfos& A, const FCandidateInfos& B)
		{
			return Priority[A.Index] > Priority[B.Index];
		});
		LastCandidatesCount = Candidates.Num();

		StartParallelLoopForRange(Candidates.Num());
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SelfPruning::ProcessRange);

		const UPCGBasePointData* InData = PointDataFacade->GetIn();
		const PCGPointOctree::FPointOctree& Octree = InData->GetPointOctree();
		TConstPCGValueRange<FTransform> Transforms = InData->GetConstTransformValueRange();

		if (Settings->Mode == EPCGExSelfPruningMode::WriteResult)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				FCandidateInfos& Candidate = Candidates[i];
				Candidate.bSkip = true;

				const int32 Index = Candidate.Index;
				const FTransform& Transform = Transforms[Index];

				FBox BoxA = InData->GetLocalBounds(Index);
				FBox Box = FBox(NoInit);
				if (Settings->PrimaryMode == EPCGExSelfPruningExpandOrder::Before)
				{
					BoxA = BoxA.ExpandBy(PrimaryExpansion->Read(Index));
					Box = BoxA.TransformBy(Transform);
				}
				else if (Settings->PrimaryMode == EPCGExSelfPruningExpandOrder::After)
				{
					Box = BoxA.TransformBy(Transform).ExpandBy(PrimaryExpansion->Read(Index));
					BoxA = BoxA.ExpandBy(PrimaryExpansion->Read(Index));
				}
				else
				{
					Box = BoxA.TransformBy(Transform);
				}

				Octree.FindElementsWithBoundsTest(Box, [&](const PCGPointOctree::FPointRef& Other)
				{
					const int32 OtherIndex = Other.Index;

					// Ignore self
					if (OtherIndex == Index || !PointFilterCache[OtherIndex])
					{
						return;
					}
					if (Box.Intersect(BoxSecondary[Other.Index]))
					{
						if (Settings->bPreciseTest)
						{
							// Use pre-built OBBs instead of constructing them each time
							if (!PCGExMath::OBB::SATOverlap(PrimaryOBBs[Index], SecondaryOBBs[OtherIndex]))
							{
								return;
							}
						}

						Candidate.Overlaps++;
					}
				});
			}
		}
		else
		{
			PCGEX_SCOPE_LOOP(i)
			{
				FCandidateInfos& Candidate = Candidates[i];
				Candidate.bSkip = true;

				const int32 Index = Candidate.Index;

				if (!PointFilterCache[Index])
				{
					continue;
				}

				const int32 CurrentPriority = Priority[Index];
				const FTransform& Transform = Transforms[Index];


				FBox BoxA = InData->GetLocalBounds(Index);
				FBox Box = FBox(NoInit);
				if (Settings->PrimaryMode == EPCGExSelfPruningExpandOrder::Before)
				{
					BoxA = BoxA.ExpandBy(PrimaryExpansion->Read(Index));
					Box = BoxA.TransformBy(Transform);
				}
				else if (Settings->PrimaryMode == EPCGExSelfPruningExpandOrder::After)
				{
					Box = BoxA.TransformBy(Transform).ExpandBy(PrimaryExpansion->Read(Index));
					BoxA = BoxA.ExpandBy(PrimaryExpansion->Read(Index));
				}
				else
				{
					Box = BoxA.TransformBy(Transform);
				}

				Octree.FindFirstElementWithBoundsTest(Box, [&](const PCGPointOctree::FPointRef& Other)
				{
					const int32 OtherIndex = Other.Index;

					// Ignore self
					if (OtherIndex == Index || !PointFilterCache[OtherIndex] || !Mask[OtherIndex])
					{
						return true;
					}

					// Ignore lower priorities, those will be pruned by this candidate when their turn comes
					if (Priority[OtherIndex] < CurrentPriority)
					{
						return true;
					}

					if (Box.Intersect(BoxSecondary[OtherIndex]))
					{
						if (Settings->bPreciseTest)
						{
							// Use pre-built OBBs instead of constructing them each time
							if (!PCGExMath::OBB::SATOverlap(PrimaryOBBs[Index], SecondaryOBBs[OtherIndex]))
							{
								return true;
							}
						}

						Mask[Index] = false;
						return false;
					}

					return true;
				});
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		if (Settings->Mode == EPCGExSelfPruningMode::WriteResult)
		{
			if (Settings->Units == EPCGExMeanMeasure::Relative)
			{
				TSharedPtr<PCGExData::TBuffer<double>> Buffer = PointDataFacade->GetWritable<double>(Settings->NumOverlapAttributeName, 0, true, PCGExData::EBufferInit::New);
				double Max = 0;
				for (int i = 0; i < Candidates.Num(); i++)
				{
					Max = FMath::Max(Max, Candidates[i].Overlaps);
				}

				if (Max == 0)
				{
					return;
				} // Shoo shoo divide by zero

				if (Settings->bOutputOneMinusOverlap)
				{
					for (const FCandidateInfos& Candidate : Candidates)
					{
						Buffer->SetValue(Candidate.Index, 1 - (static_cast<double>(Candidate.Overlaps) / Max));
					}
				}
				else
				{
					for (const FCandidateInfos& Candidate : Candidates)
					{
						Buffer->SetValue(Candidate.Index, (static_cast<double>(Candidate.Overlaps) / Max));
					}
				}
			}
			else
			{
				TSharedPtr<PCGExData::TBuffer<int32>> Buffer = PointDataFacade->GetWritable<int32>(Settings->NumOverlapAttributeName, 0, true, PCGExData::EBufferInit::New);
				for (const FCandidateInfos& Candidate : Candidates)
				{
					Buffer->SetValue(Candidate.Index, Candidate.Overlaps);
				}
			}

			return;
		}

		int32 WriteIndex = 0;
		for (int32 i = 0; i < Candidates.Num(); i++)
		{
			FCandidateInfos& Candidate = Candidates[i];
			if (!Candidate.bSkip)
			{
				Candidates[WriteIndex++] = MoveTemp(Candidate);
			}
		}

		Candidates.SetNum(WriteIndex);

		if (!Candidates.IsEmpty())
		{
			if (LastCandidatesCount == WriteIndex)
			{
				// Error
				// Somehow the last run could not get rid of some overlap, number of candidates hasn't changed
				// We have to stop iterating now or we'll iterate forever
				return;
			}

			LastCandidatesCount = WriteIndex;
			StartParallelLoopForRange(WriteIndex);
		}
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->Mode == EPCGExSelfPruningMode::WriteResult)
		{
			PointDataFacade->WriteFastest(TaskManager);
			return;
		}

		bool bAnyPruning = false;
		for (int32 i = 0; i < Mask.Num(); i++)
		{
			if (!Mask[i])
			{
				bAnyPruning = true;
				break;
			}
		}

		if (!bAnyPruning)
		{
			PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			return;
		}

		PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		// Mask is the keep-set; the pruned-away points are its inverse. Built from In before
		// Gather compacts Out (Gather only touches Out, so ordering is irrelevant).
		if (Context->Discarded)
		{
			if (const TSharedPtr<PCGExData::FPointIO> Discarded = Context->Discarded->Emplace_GetRef(PointDataFacade->GetIn(), PCGExData::EIOInit::New))
			{
				(void)Discarded->InheritPoints(Mask, true);
			}
		}

		(void)PointDataFacade->Source->Gather(Mask);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
