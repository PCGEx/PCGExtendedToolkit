// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathStitch.h"
#include "PCGExVersion.h"

#include "PCGExMatchingCommon.h"
#include "PCGExOctree.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Paths/PCGExPathsHelpers.h"

#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"
#include "Utils/PCGExPointIOMerger.h"

#define LOCTEXT_NAMESPACE "PCGExPathStitchElement"
#define PCGEX_NAMESPACE PathStitch

TArray<FPCGPinProperties> UPCGExPathStitchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(MatchingDetails, PinProperties);
	// OnInitialPostProcess reads PCGExSorting::Labels::SourceSortingRules, so that is the pin that has to exist.
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Normal);
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathStitch)

#if WITH_EDITOR
void UPCGExPathStitchSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		MatchingDetails.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExPathStitchSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		MatchingDetails.ApplyDeprecation();
	}

	PCGEX_IF_VERSION_LOWER(1, 78, 7)
	{
		// Graphs saved before self-closure existed keep their open outputs.
		bCloseSelfConnectingPaths = false;
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(PathStitch)

bool FPCGExPathStitchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PathStitch)

	PCGEX_FWD(DotComparisonDetails)
	Context->DotComparisonDetails.Init();

	PCGEX_FWD(MatchingDetails)

	Context->Datas.Reset(Context->MainPoints->Pairs.Num());

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	PCGEX_FWD(bTagToAttributes)
	PCGEX_FWD(TagsToAttributes)
	Context->TagsToAttributes.Init();

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

				PCGEX_SKIP_INVALID_PATH_ENTRY

				FPCGTaggedData& D = Context->Datas.Emplace_GetRef();
				D.Data = Entry->GetIn();
				Entry->Tags->DumpTo(D.Tags);
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
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
	bool FProcessor::SetStartStitch(const TSharedPtr<FProcessor>& InStitch, const bool bToEnd)
	{
		if (StartStitch.IsValid())
		{
			return false;
		}
		StartStitch = InStitch;
		bStartStitchToEnd = bToEnd;
		return true;
	}

	bool FProcessor::SetEndStitch(const TSharedPtr<FProcessor>& InStitch, const bool bToEnd)
	{
		if (EndStitch.IsValid())
		{
			return false;
		}
		EndStitch = InStitch;
		bEndStitchToEnd = bToEnd;
		return true;
	}

	bool FProcessor::DropsEndpointOnSelfClosure() const
	{
		return FVector::Dist(StartSegment.B, EndSegment.B) <= Settings->SelfCoincidenceTolerance;
	}

	void FProcessor::CloseSelf()
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;
		PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::Duplicate);

		if (DropsEndpointOnSelfClosure())
		{
			const bool bDropStart = Settings->Method == EPCGExStitchMethod::Fuse && Settings->FuseMethod == EPCGExStitchFuseMethod::KeepEnd;

			TArray<int32> KeptIndices;
			PCGExArrayHelpers::ArrayOfIndices(KeptIndices, PointIO->GetNum() - 1, bDropStart ? 1 : 0);
			PointIO->Gather(KeptIndices);
		}

		PCGExPaths::Helpers::SetClosedLoop(PointIO->GetOut(), true);
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathStitch::Process);

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}
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
		if (!EndStitch.IsValid() && !StartStitch.IsValid())
		{
			PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward);
			return;
		}

		if (EndStitch.HasSameObject(this))
		{
			CloseSelf();
			return;
		}

		bool bClosedLoop = false;

		const TSharedPtr<FProcessor> Start = SharedThis(this);

		// Direction comes from the recorded peer endpoint, never from comparing stitch pointers:
		// in a 2-path closed loop both stitches of a link alias the same processor.
		struct FChainLink
		{
			TSharedPtr<FProcessor> Processor;
			bool bReverse;
		};

		TArray<FChainLink> Chain;
		Chain.Reserve(8);

		// Prefer EndStitch when both are set; otherwise we are an endpoint.
		const bool bStartGoesViaEnd = EndStitch.IsValid();
		TSharedPtr<FProcessor> NextProcessor = bStartGoesViaEnd ? EndStitch.Pin() : StartStitch.Pin();
		bool bEnterViaEnd = bStartGoesViaEnd ? bEndStitchToEnd : bStartStitchToEnd;

		Chain.Add({Start, !bStartGoesViaEnd});

		int32 SmallestWorkIndex = WorkIndex;

		while (NextProcessor)
		{
			// A link entered through its end is read backward and left through its start.
			const TSharedPtr<FProcessor> Current = NextProcessor;

			Chain.Add({Current, bEnterViaEnd});
			SmallestWorkIndex = FMath::Min(SmallestWorkIndex, Current->WorkIndex);

			NextProcessor = bEnterViaEnd ? Current->StartStitch.Pin() : Current->EndStitch.Pin();
			bEnterViaEnd = bEnterViaEnd ? Current->bStartStitchToEnd : Current->bEndStitchToEnd;

			if (NextProcessor == Start)
			{
				bClosedLoop = true;
				NextProcessor = nullptr;
			}
		}

		if (EndStitch.IsValid() && StartStitch.IsValid())
		{
			// Closed-loop members all land here -- only the SmallestWorkIndex one
			// proceeds; mid-chain processors of open chains also bail out.
			if (!bClosedLoop || WorkIndex != SmallestWorkIndex)
			{
				return;
			}
		}
		else
		{
			// Open chain endpoint: lower-WorkIndex endpoint wins; the other bails
			// so the merge runs once.
			if (Chain.Last().Processor->WorkIndex < WorkIndex)
			{
				return;
			}
		}

		PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::New);
		Merger = MakeShared<FPCGExPointIOMerger>(PointDataFacade);

		const bool bFuse = Settings->Method == EPCGExStitchMethod::Fuse;
		const bool bKeepEnd = Settings->FuseMethod == EPCGExStitchFuseMethod::KeepEnd;
		const int32 LastLink = Chain.Num() - 1;

		for (int i = 0; i < Chain.Num(); ++i)
		{
			const FChainLink& Link = Chain[i];
			const TSharedPtr<FProcessor>& Current = Link.Processor;

			// Each junction drops one point, in chain order: Keep Start the previous link's last, Keep End the next link's first.
			const bool bTrimHead = bFuse && bKeepEnd && (i > 0 || bClosedLoop);
			const bool bTrimTail = bFuse && !bKeepEnd && (i < LastLink || bClosedLoop);

			// The read scope is in native order: a reversed link's chain tail is its native first point.
			const int32 ReadStart = (Link.bReverse ? bTrimTail : bTrimHead) ? 1 : 0;
			const int32 ReadCount = Current->PointDataFacade->GetNum() - (bTrimHead || bTrimTail ? 1 : 0);

			PCGExPointIOMerger::FMergeScope& MergeScope = Merger->Append(Current->PointDataFacade->Source, static_cast<PCGExMT::FScope>(Current->PointDataFacade->GetInScope(ReadStart, ReadCount)));

			MergeScope.bReverse = Link.bReverse;
		}

		Merger->MergeAsync(TaskManager, &Context->CarryOverDetails, nullptr, false, PCGExDataFilter::ResolveOptional(Settings->bTagToAttributes, Context->TagsToAttributes));

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
				// Stable: ties keep arrival order.
				SortedProcessors.StableSort([&](const TSharedPtr<FProcessor>& A, const TSharedPtr<FProcessor>& B)
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
			for (const TSharedPtr<FProcessor>& P : SortedProcessors)
			{
				Facades.Add(P->PointDataFacade);
			}

			TSharedPtr<PCGExMatching::FDataMatcher> Matcher = MakeShared<PCGExMatching::FDataMatcher>();
			Matcher->SetDetails(&Context->MatchingDetails);

			if (Matcher->Init(Context, Facades, false, PCGExMatching::Labels::SourceMatchRulesLabel))
			{
				TArray<TArray<int32>> Partitions;
				PCGExMatching::Helpers::GetMatchingSourcePartitions(Matcher, Partitions, true);

				// Each sorted index gets a partition ID; unmatched get unique negative IDs
				PartitionOf.SetNum(SortedProcessors.Num());
				for (int32 Idx = 0; Idx < PartitionOf.Num(); ++Idx)
				{
					PartitionOf[Idx] = -(Idx + 1);
				}

				for (int32 PIdx = 0; PIdx < Partitions.Num(); ++PIdx)
				{
					for (const int32 Idx : Partitions[PIdx])
					{
						PartitionOf[Idx] = PIdx;
					}
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

					const bool bSelf = Other == Current;

					if (bSelf)
					{
						// The self pair is collected once, end to start.
						if (!Settings->bCloseSelfConnectingPaths || !bCurrentEnd || bOtherEnd)
						{
							return;
						}
					}
					else if (Other->WorkIndex < Current->WorkIndex)
					{
						return; // Dedup
					}

					if (!PartitionOf.IsEmpty() && PartitionOf[Current->WorkIndex] != PartitionOf[Other->WorkIndex])
					{
						return;
					}

					if (Settings->bOnlyMatchStartAndEnds && bCurrentEnd == bOtherEnd)
					{
						return;
					}

					const PCGExMath::FSegment& OtherSeg = bOtherEnd ? Other->EndSegment : Other->StartSegment;

					const double Dist = FVector::Dist(CurrentSeg.B, OtherSeg.B);
					if (Dist > Settings->Tolerance)
					{
						return;
					}

					// A closed loop needs 3 points left once a coincident endpoint is dropped.
					if (bSelf && Current->PointDataFacade->GetNum() - (Current->DropsEndpointOnSelfClosure() ? 1 : 0) < 3)
					{
						return;
					}

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
							// Endpoints coincident -- bridge direction undefined, fall back to segment alignment
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

					if (Settings->bDoRequireAlignment && Settings->bStrictAlignment && !Context->DotComparisonDetails.Test(Alignment))
					{
						return;
					}

					Candidates.Add({Current, Other, bCurrentEnd, bOtherEnd, Alignment, Dist});
				});
			};

			CollectFromEndpoint(true);
			CollectFromEndpoint(false);
		}

		// Phase 2: Sort -- best alignment (when enabled), then closest, with deterministic tiebreaker

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
			if (X.A->WorkIndex != Y.A->WorkIndex)
			{
				return X.A->WorkIndex < Y.A->WorkIndex;
			}
			return X.B->WorkIndex < Y.B->WorkIndex;
		});

		// Phase 3: Greedily assign best matches, skipping claimed endpoints

		for (const FStitchCandidate& C : Candidates)
		{
			if ((C.bEndA ? C.A->EndStitch : C.A->StartStitch).IsValid() ||
				(C.bEndB ? C.B->EndStitch : C.B->StartStitch).IsValid())
			{
				continue;
			}

			const bool bA = C.bEndA ? C.A->SetEndStitch(C.B, C.bEndB) : C.A->SetStartStitch(C.B, C.bEndB);
			const bool bB = C.bEndB ? C.B->SetEndStitch(C.A, C.bEndA) : C.B->SetStartStitch(C.A, C.bEndA);
			check(bA && bB);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
