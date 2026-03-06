// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathStitch.h"

#include "PCGExOctree.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Data/PCGExDataTags.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatchingCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Utils/PCGExPointIOMerger.h"

#define LOCTEXT_NAMESPACE "PCGExPathStitchElement"
#define PCGEX_NAMESPACE PathStitch

TArray<FPCGPinProperties> UPCGExPathStitchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(MatchingDetails, PinProperties);
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeSortingRules, "Sort-in-place to order the data if needed", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathStitch)
PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(PathStitch)

bool FPCGExPathStitchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PathStitch)

	PCGEX_FWD(DotComparisonDetails)
	Context->DotComparisonDetails.Init();

	PCGEX_FWD(MatchingDetails)

	Context->Datas.Reset(Context->MainPoints->Pairs.Num());

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	return true;
}

bool FPCGExPathStitchElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathStitchElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathStitch)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (PCGExPaths::Helpers::GetClosedLoop(Entry->GetIn()))
				{
					Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					return false;
				}

				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}

				FPCGTaggedData& D = Context->Datas.Emplace_GetRef();
				D.Data = Entry->GetIn();
				Entry->Tags->DumpTo(D.Tags);
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				//NewBatch->SetPointsFilterData(&Context->FilterFactories);
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to work with."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExPathStitch
{
	bool FProcessor::IsStitchedTo(const TSharedPtr<FProcessor>& InOtherProcessor)
	{
		const TSharedPtr<FProcessor> Self = SharedThis(this);
		return StartStitch == InOtherProcessor || EndStitch == InOtherProcessor || InOtherProcessor->StartStitch == Self || InOtherProcessor->EndStitch == Self;
	}

	bool FProcessor::SetStartStitch(const TSharedPtr<FProcessor>& InStitch)
	{
		if (StartStitch) { return false; }
		StartStitch = InStitch;
		return true;
	}

	bool FProcessor::SetEndStitch(const TSharedPtr<FProcessor>& InStitch)
	{
		if (EndStitch) { return false; }
		EndStitch = InStitch;
		return true;
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathStitch::Process);

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		if (!IProcessor::Process(InTaskManager)) { return false; }
		const TConstPCGValueRange<FTransform> InTransform = PointDataFacade->GetIn()->GetConstTransformValueRange();

		const FVector Extents = FVector::OneVector * 0.5;

		StartSegment = PCGExMath::FSegment(InTransform[1].GetLocation(), InTransform[0].GetLocation(), Settings->Tolerance);
		StartBounds = FBox(StartSegment.B + Extents * -Settings->Tolerance, StartSegment.B + Extents * Settings->Tolerance);

		EndSegment = PCGExMath::FSegment(InTransform[InTransform.Num() - 2].GetLocation(), InTransform[InTransform.Num() - 1].GetLocation(), Settings->Tolerance);
		EndBounds = FBox(EndSegment.B + Extents * -Settings->Tolerance, EndSegment.B + Extents * Settings->Tolerance);

		return true;
	}

	void FProcessor::CompleteWork()
	{
		if (!EndStitch && !StartStitch)
		{
			PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward);
			return;
		}

		bool bClosedLoop = false;

		TSharedPtr<FProcessor> Start = SharedThis(this);
		TSharedPtr<FProcessor> PreviousProcessor = Start;
		TSharedPtr<FProcessor> NextProcessor = EndStitch ? EndStitch : StartStitch;

		TArray<TSharedPtr<FProcessor>> Chain;
		Chain.Add(Start);

		int32 SmallestWorkIndex = WorkIndex;

		// Rebuild the chain
		while (NextProcessor)
		{
			Chain.Add(NextProcessor);
			SmallestWorkIndex = FMath::Min(SmallestWorkIndex, NextProcessor->WorkIndex);

			TSharedPtr<FProcessor> OldPrev = PreviousProcessor;
			PreviousProcessor = NextProcessor;
			NextProcessor = NextProcessor->StartStitch == OldPrev ? NextProcessor->EndStitch : NextProcessor->StartStitch;

			if (NextProcessor == Start)
			{
				// That's a closed loop!
				bClosedLoop = true;
				NextProcessor = nullptr;
			}
		}

		// Mid-path, will be merged
		if (EndStitch && StartStitch)
		{
			if (!bClosedLoop || WorkIndex != SmallestWorkIndex) { return; }
		}

		if (bClosedLoop)
		{
			// Nullify start so we go in order
			StartStitch = nullptr;

			if (Chain.Last()->StartStitch == Start) { Chain.Last()->StartStitch = nullptr; }
			else if (Chain.Last()->EndStitch == Start) { Chain.Last()->EndStitch = nullptr; }
		}
		else
		{
			if (Chain.Last()->WorkIndex < WorkIndex) { return; }
		}

		// Other work index is smaller, will do the resolve.

		PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::New);
		Merger = MakeShared<FPCGExPointIOMerger>(PointDataFacade);

		for (int i = 0; i < Chain.Num(); ++i)
		{
			const TSharedPtr<FProcessor> Current = Chain[i];
			const TSharedPtr<FProcessor> Previous = i == 0 ? nullptr : Chain[i - 1];

			int32 ReadStart = 0;
			int32 ReadCount = Current->PointDataFacade->GetNum();

			if (Settings->Method == EPCGExStitchMethod::Fuse)
			{
				const bool bIsLast = i == Chain.Num() - 1;

				if (!bIsLast || bClosedLoop) { ReadCount--; }

				if (Settings->FuseMethod == EPCGExStitchFuseMethod::KeepEnd)
				{
					if (!bIsLast || bClosedLoop) { ReadStart++; }
				}
				else
				{
				}
			}

			PCGExPointIOMerger::FMergeScope& MergeScope = Merger->Append(Current->PointDataFacade->Source, static_cast<PCGExMT::FScope>(Current->PointDataFacade->GetInScope(ReadStart, ReadCount)));

			MergeScope.bReverse = i == 0 ? Current->EndStitch == nullptr : Current->StartStitch != Previous;
		}

		Merger->MergeAsync(TaskManager, &Context->CarryOverDetails);

		PCGExPaths::Helpers::SetClosedLoop(PointDataFacade->GetOut(), bClosedLoop);
	}

	void FProcessor::Write()
	{
		if (!PointDataFacade->Source->IsForwarding())
		{
			// TODO : Stitch-Merge points (average etc)
			PointDataFacade->WriteFastest(TaskManager);
		}
	}

	FBatch::FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
		: TBatch(InContext, InPointsCollection)
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PathStitch);
	}

	void FBatch::OnInitialPostProcess()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PathStitch);

		TBatch<FProcessor>::OnInitialPostProcess();

		TArray<TSharedPtr<FProcessor>> SortedProcessors;
		SortedProcessors.Reserve(Processors.Num());

		FBox OctreeBounds = FBox(ForceInit);
		for (int Pi = 0; Pi < Processors.Num(); Pi++)
		{
			const TSharedPtr<FProcessor> P = GetProcessor<FProcessor>(Pi);
			SortedProcessors.Add(P);
			OctreeBounds += P->StartBounds;
			OctreeBounds += P->EndBounds;
		}

		// Attempt to sort -- if it fails it's ok, just throw a warning
		TArray<FPCGExSortRuleConfig> RuleConfigs = PCGExSorting::GetSortingRules(Context, PCGExSorting::Labels::SourceSortingRules);
		if (!RuleConfigs.IsEmpty())
		{
			const TSharedPtr<PCGExSorting::FSorter> Sorter = MakeShared<PCGExSorting::FSorter>(RuleConfigs);
			Sorter->SortDirection = Settings->SortDirection;

			if (Sorter->Init(Context, Context->Datas))
			{
				SortedProcessors.Sort([&](const TSharedPtr<FProcessor>& A, const TSharedPtr<FProcessor>& B)
				{
					return Sorter->SortData(A->BatchIndex, B->BatchIndex);
				});
			}
			else
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Problem with initializing sorting rules."));
			}
		}

		// Build data octree
		TSharedPtr<PCGExOctree::FItemOctree> PathOctree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
		for (int i = 0; i < SortedProcessors.Num(); ++i)
		{
			const TSharedPtr<FProcessor> Processor = SortedProcessors[i];
			Processor->WorkIndex = i;

			// -1 <------> +1
			PathOctree->AddElement(PCGExOctree::FItem((Processor->BatchIndex + 1) * -1, Processor->StartBounds));
			PathOctree->AddElement(PCGExOctree::FItem((Processor->BatchIndex + 1), Processor->EndBounds));
		}

		// Build partition lookup from matching rules (empty = matching disabled, all paths can stitch)

		TArray<int32> PartitionOf;

		if (Context->MatchingDetails.IsEnabled())
		{
			TArray<TSharedPtr<PCGExData::FFacade>> Facades;
			Facades.Reserve(SortedProcessors.Num());
			for (const TSharedPtr<FProcessor>& P : SortedProcessors) { Facades.Add(P->PointDataFacade); }

			TSharedPtr<PCGExMatching::FDataMatcher> Matcher = MakeShared<PCGExMatching::FDataMatcher>();
			Matcher->SetDetails(&Context->MatchingDetails);

			if (Matcher->Init(Context, Facades, false, PCGExMatching::Labels::SourceMatchRulesLabel))
			{
				TArray<TArray<int32>> Partitions;
				PCGExMatching::Helpers::GetMatchingSourcePartitions(Matcher, Facades, Partitions, true);

				// Each sorted index gets a partition ID; unmatched get unique negative IDs
				PartitionOf.SetNum(SortedProcessors.Num());
				for (int32 Idx = 0; Idx < PartitionOf.Num(); ++Idx) { PartitionOf[Idx] = -(Idx + 1); }

				for (int32 PIdx = 0; PIdx < Partitions.Num(); ++PIdx)
				{
					for (const int32 Idx : Partitions[PIdx]) { PartitionOf[Idx] = PIdx; }
				}
			}
		}

		// Phase 1: Collect all valid stitch candidates

		struct FStitchCandidate
		{
			TSharedPtr<FProcessor> A;
			TSharedPtr<FProcessor> B;
			bool bEndA;
			bool bEndB;
			double Alignment;
			double Dist;
		};

		TArray<FStitchCandidate> Candidates;

		for (int i = 0; i < SortedProcessors.Num(); ++i)
		{
			const TSharedPtr<FProcessor>& Current = SortedProcessors[i];

			auto CollectFromEndpoint = [&](const bool bCurrentEnd)
			{
				const FBox& QueryBounds = bCurrentEnd ? Current->EndBounds : Current->StartBounds;
				const PCGExMath::FSegment& CurrentSeg = bCurrentEnd ? Current->EndSegment : Current->StartSegment;

				PathOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
				{
					const bool bOtherEnd = Item.Index > 0;
					const int32 OtherBatchIndex = FMath::Abs(Item.Index) - 1;
					const TSharedPtr<FProcessor> Other = GetProcessor<FProcessor>(OtherBatchIndex);

					if (Other->WorkIndex <= Current->WorkIndex) { return; } // Dedup + skip self

					if (!PartitionOf.IsEmpty() && PartitionOf[Current->WorkIndex] != PartitionOf[Other->WorkIndex]) { return; }

					if (Settings->bOnlyMatchStartAndEnds && bCurrentEnd == bOtherEnd) { return; }

					const PCGExMath::FSegment& OtherSeg = bOtherEnd ? Other->EndSegment : Other->StartSegment;

					const double Dist = FVector::Dist(CurrentSeg.B, OtherSeg.B);
					if (Dist > Settings->Tolerance) { return; }

					// Compute alignment based on mode
					double Alignment;
					const double SegmentDot = FVector::DotProduct(CurrentSeg.Direction, OtherSeg.Direction * -1);

					if (Settings->AlignmentMode == EPCGExStitchAlignmentMode::Segments)
					{
						Alignment = SegmentDot;
					}
					else
					{
						const FVector BridgeDir = (OtherSeg.B - CurrentSeg.B).GetSafeNormal();

						if (BridgeDir.IsNearlyZero())
						{
							// Endpoints coincident — bridge direction undefined, fall back to segment alignment
							Alignment = SegmentDot;
						}
						else
						{
							const double BridgeDotA = FVector::DotProduct(CurrentSeg.Direction, BridgeDir);
							const double BridgeDotB = FVector::DotProduct(OtherSeg.Direction * -1, BridgeDir);

							const double BridgeScore = Settings->BridgeScoring == EPCGExStitchBridgeScoring::Minimum
								? FMath::Min(BridgeDotA, BridgeDotB)
								: (BridgeDotA + BridgeDotB) * 0.5;

							Alignment = Settings->AlignmentMode == EPCGExStitchAlignmentMode::Bridge
								? BridgeScore
								: FMath::Min(SegmentDot, BridgeScore);
						}
					}

					if (Settings->bDoRequireAlignment && Settings->bStrictAlignment && !Context->DotComparisonDetails.Test(Alignment)) { return; }

					Candidates.Add({Current, Other, bCurrentEnd, bOtherEnd, Alignment, Dist});
				});
			};

			CollectFromEndpoint(true);
			CollectFromEndpoint(false);
		}

		// Phase 2: Sort — best alignment (when enabled), then closest, with deterministic tiebreaker

		const bool bSortByAlignment = Settings->bDoRequireAlignment;
		Candidates.Sort([bSortByAlignment](const FStitchCandidate& X, const FStitchCandidate& Y)
		{
			if (bSortByAlignment && !FMath::IsNearlyEqual(X.Alignment, Y.Alignment, KINDA_SMALL_NUMBER))
			{
				return X.Alignment > Y.Alignment;
			}
			if (!FMath::IsNearlyEqual(X.Dist, Y.Dist, KINDA_SMALL_NUMBER))
			{
				return X.Dist < Y.Dist;
			}
			if (X.A->WorkIndex != Y.A->WorkIndex) { return X.A->WorkIndex < Y.A->WorkIndex; }
			return X.B->WorkIndex < Y.B->WorkIndex;
		});

		// Phase 3: Greedily assign best matches, skipping claimed endpoints

		for (const FStitchCandidate& C : Candidates)
		{
			if ((C.bEndA ? C.A->EndStitch : C.A->StartStitch) ||
				(C.bEndB ? C.B->EndStitch : C.B->StartStitch))
			{
				continue;
			}

			const bool bA = C.bEndA ? C.A->SetEndStitch(C.B) : C.A->SetStartStitch(C.B);
			const bool bB = C.bEndB ? C.B->SetEndStitch(C.A) : C.B->SetStartStitch(C.A);
			check(bA && bB);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
