// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleOverlapStats.h"

#include "Math/PCGExMathBounds.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Sampling/PCGExSamplingHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExSampleOverlapStatsElement"
#define PCGEX_NAMESPACE SampleOverlapStats

TSharedPtr<PCGExSampleOverlapStats::FOverlap> FPCGExSampleOverlapStatsContext::RegisterOverlap(PCGExSampleOverlapStats::FProcessor* InA, PCGExSampleOverlapStats::FProcessor* InB, const FBox& InIntersection)
{
	const uint64 HashID = PCGEx::H64U(InA->BatchIndex, InB->BatchIndex);

	{
		FReadScopeLock ReadScopeLock(OverlapLock);
		if (TSharedPtr<PCGExSampleOverlapStats::FOverlap>* FoundPtr = OverlapMap.Find(HashID))
		{
			return *FoundPtr;
		}
	}

	{
		FWriteScopeLock WriteScopeLock(OverlapLock);
		if (TSharedPtr<PCGExSampleOverlapStats::FOverlap>* FoundPtr = OverlapMap.Find(HashID))
		{
			return *FoundPtr;
		}

		const bool bInvert = InA->BatchIndex > InB->BatchIndex;
		PCGEX_MAKE_SHARED(NewOverlap, PCGExSampleOverlapStats::FOverlap, bInvert ? InB : InA, bInvert ? InA : InB, InIntersection)
		OverlapMap.Add(HashID, NewOverlap);
		return NewOverlap;
	}
}

void FPCGExSampleOverlapStatsContext::BatchProcessing_WorkComplete()
{
	FPCGExPointsProcessorContext::BatchProcessing_WorkComplete();

	const TSharedPtr<PCGExPointsMT::TBatch<PCGExSampleOverlapStats::FProcessor>> TypedBatch = StaticCastSharedPtr<PCGExPointsMT::TBatch<PCGExSampleOverlapStats::FProcessor>>(MainBatch);

	for (int Pi = 0; Pi < TypedBatch->GetNumProcessors(); Pi++)
	{
		const TSharedPtr<PCGExSampleOverlapStats::FProcessor> P = TypedBatch->GetProcessor<PCGExSampleOverlapStats::FProcessor>(Pi);
		if (!P->bIsProcessorValid)
		{
			continue;
		}
		SharedOverlapSubCountMax = FMath::Max(SharedOverlapSubCountMax, P->LocalOverlapSubCountMax);
		SharedOverlapCountMax = FMath::Max(SharedOverlapCountMax, P->LocalOverlapCountMax);
	}
}

PCGEX_INITIALIZE_ELEMENT(SampleOverlapStats)

PCGExData::EIOInit UPCGExSampleOverlapStatsSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleOverlapStats)

bool FPCGExSampleOverlapStatsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}


	PCGEX_CONTEXT_AND_SETTINGS(SampleOverlapStats)

	PCGEX_FOREACH_FIELD_SAMPLEOVERLAPSTATS(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Context->MainPoints->Num() < 2)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Not enough inputs; requires at least 2 to check for overlap."));
		return false;
	}

	return true;
}

bool FPCGExSampleOverlapStatsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleOverlapStatsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleOverlapStats)
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
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any input to check for overlaps."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleOverlapStats
{
	FOverlap::FOverlap(FProcessor* InPrimary, FProcessor* InSecondary, const FBox& InIntersection)
		: Intersection(InIntersection)
		  , Primary(InPrimary)
		  , Secondary(InSecondary)
	{
		HashID = PCGEx::H64U(InPrimary->BatchIndex, InSecondary->BatchIndex);
	}

	FProcessor::~FProcessor()
	{
	}

	void FProcessor::RegisterOverlap(FProcessor* InOtherProcessor, const FBox& Intersection)
	{
		FWriteScopeLock WriteScopeLock(RegistrationLock);
		const TSharedPtr<FOverlap> Overlap = Context->RegisterOverlap(this, InOtherProcessor, Intersection);
		if (Overlap->Primary == this)
		{
			ManagedOverlaps.Add(Overlap.ToSharedRef());
		}
		Overlaps.Add(Overlap.ToSharedRef());
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_SAMPLEOVERLAPSTATS(PCGEX_OUTPUT_INIT)
		}

		// 1 - Build bounds & octrees

		InPoints = PointDataFacade->GetIn();
		NumPoints = InPoints->GetNumPoints();

		LocalPointBounds.Init(nullptr, NumPoints);
		OverlapSubCount.Init(0, NumPoints);
		OverlapCount.Init(0, NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);
		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

#define PCGEX_POINT_CHECK if (!PointFilterCache[i]) { continue; } const PCGExData::FConstPoint Point(InPoints, i);

		if (Settings->BoundsSource == EPCGExPointBoundsSource::ScaledBounds)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				PCGEX_POINT_CHECK
				const FBox LocalBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point).ExpandBy(Settings->Expansion);
				TSharedPtr<PCGExDiscardByOverlap::FPointBounds> PtBounds = MakeShared<PCGExDiscardByOverlap::FPointBounds>(i, Point, LocalBounds);
				RegisterPointBounds(i, PtBounds);
			}
		}
		else if (Settings->BoundsSource == EPCGExPointBoundsSource::DensityBounds)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				PCGEX_POINT_CHECK

				const FBox LocalBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::DensityBounds>(Point).ExpandBy(Settings->Expansion);
				TSharedPtr<PCGExDiscardByOverlap::FPointBounds> PtBounds = MakeShared<PCGExDiscardByOverlap::FPointBounds>(i, Point, LocalBounds);
				RegisterPointBounds(i, PtBounds);
			}
		}
		else if (Settings->BoundsSource == EPCGExPointBoundsSource::Bounds)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				PCGEX_POINT_CHECK
				const FBox LocalBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::Bounds>(Point).ExpandBy(Settings->Expansion);
				TSharedPtr<PCGExDiscardByOverlap::FPointBounds> PtBounds = MakeShared<PCGExDiscardByOverlap::FPointBounds>(i, Point, LocalBounds);
				RegisterPointBounds(i, PtBounds);
			}
		}
		else if (Settings->BoundsSource == EPCGExPointBoundsSource::Center)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				PCGEX_POINT_CHECK
				const FBox LocalBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::Center>(Point).ExpandBy(Settings->Expansion);
				TSharedPtr<PCGExDiscardByOverlap::FPointBounds> PtBounds = MakeShared<PCGExDiscardByOverlap::FPointBounds>(i, Point, LocalBounds);
				RegisterPointBounds(i, PtBounds);
			}
		}

#undef PCGEX_POINT_CHECK
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		Octree = MakeShared<PCGExDiscardByOverlap::FPointBoundsOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length());
		for (const TSharedPtr<PCGExDiscardByOverlap::FPointBounds>& PtBounds : LocalPointBounds)
		{
			if (!PtBounds)
			{
				continue;
			}
			Octree->AddElement(PtBounds.Get());
		}
	}

	void FProcessor::ResolveOverlap(const int32 Index)
	{
		// For each managed overlap, find per-point intersections

		const TSharedPtr<FOverlap> Overlap = Overlaps[Index];
		const bool bUpdateOverlap = ManagedOverlaps.Contains(Overlap);
		const TSharedRef<FProcessor> OtherProcessor = StaticCastSharedRef<FProcessor>(*ParentBatch.Pin()->SubProcessorMap->Find(&Overlap->GetOther(this)->PointDataFacade->Source.Get()));

		TConstPCGValueRange<FTransform> InTransforms = InPoints->GetConstTransformValueRange();

		if (Settings->TestMode != EPCGExOverlapTestMode::Sphere)
		{
			Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Overlap->Intersection.GetCenter(), Overlap->Intersection.GetExtent()), [&](const PCGExDiscardByOverlap::FPointBounds* OwnedPoint)
			{
				const double Length = OwnedPoint->LocalBounds.GetExtent().Length() * 2;
				const FMatrix InvMatrix = InTransforms[OwnedPoint->Index].ToMatrixNoScale().Inverse();

				int32 Count = 0;

				OtherProcessor->GetOctree()->FindElementsWithBoundsTest(FBoxCenterAndExtent(OwnedPoint->Bounds.GetBox()), [&](const PCGExDiscardByOverlap::FPointBounds* OtherPoint)
				{
					const FBox Intersection = OwnedPoint->LocalBounds.Overlap(OtherPoint->TransposedBounds(InvMatrix));

					if (!Intersection.IsValid)
					{
						return;
					}

					const double Amount = Intersection.GetExtent().Length() * 2;
					if (Settings->ThresholdMeasure == EPCGExMeanMeasure::Relative)
					{
						if ((Amount / Length) < Settings->MinThreshold)
						{
							return;
						}
					}
					else if (Amount < Settings->MinThreshold)
					{
						return;
					}

					Count++;

					if (bUpdateOverlap)
					{
						Overlap->Stats.OverlapCount++;
						Overlap->Stats.OverlapVolume += Intersection.GetVolume();
					}
				});

				if (Count > 0)
				{
					FPlatformAtomics::InterlockedExchange(&bAnyOverlap, 1);
					FPlatformAtomics::InterlockedAdd(&OverlapSubCount[OwnedPoint->Index], Count);
					FPlatformAtomics::InterlockedAdd(&OverlapCount[OwnedPoint->Index], 1);
				}
			});
		}
		else
		{
			Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Overlap->Intersection.GetCenter(), Overlap->Intersection.GetExtent()), [&](const PCGExDiscardByOverlap::FPointBounds* OwnedPoint)
			{
				const FSphere S1 = OwnedPoint->Bounds.GetSphere();

				int32 Count = 0;

				OtherProcessor->GetOctree()->FindElementsWithBoundsTest(FBoxCenterAndExtent(OwnedPoint->Bounds.GetBox()), [&](const PCGExDiscardByOverlap::FPointBounds* OtherPoint)
				{
					double Amount = 0;
					if (!PCGExMath::SphereOverlap(S1, OtherPoint->Bounds.GetSphere(), Amount))
					{
						return;
					}

					if (Settings->ThresholdMeasure == EPCGExMeanMeasure::Relative)
					{
						if ((Amount / S1.W) < Settings->MinThreshold)
						{
							return;
						}
					}
					else if (Amount < Settings->MinThreshold)
					{
						return;
					}

					Count++;

					if (bUpdateOverlap)
					{
						Overlap->Stats.OverlapCount++;
						Overlap->Stats.OverlapVolume += Amount;
					}
				});

				if (Count > 0)
				{
					FPlatformAtomics::InterlockedExchange(&bAnyOverlap, 1);
					FPlatformAtomics::InterlockedAdd(&OverlapSubCount[OwnedPoint->Index], Count);
					FPlatformAtomics::InterlockedAdd(&OverlapCount[OwnedPoint->Index], 1);
				}
			});
		}
	}

	void FProcessor::WriteSingleData(const int32 Index)
	{
		const int32 TOC = OverlapSubCount[Index];
		const int32 UOC = OverlapCount[Index];

		PCGEX_OUTPUT_VALUE(OverlapSubCount, Index, TOC)
		PCGEX_OUTPUT_VALUE(OverlapCount, Index, UOC)
		PCGEX_OUTPUT_VALUE(RelativeOverlapSubCount, Index, static_cast<double>(TOC) / Context->SharedOverlapSubCountMax)
		PCGEX_OUTPUT_VALUE(RelativeOverlapCount, Index, static_cast<double>(UOC) / Context->SharedOverlapCountMax)
	}

	void FProcessor::CompleteWork()
	{
		// 2 - Find overlaps between large bounds, we'll be searching only there.

		const TSharedPtr<PCGExPointsMT::IBatch> LocalParent = ParentBatch.Pin();
		const TArray<TSharedRef<PCGExData::FFacade>>& ProcessorFacades = ParentBatch.Pin()->ProcessorFacades;
		const int32 NumOtherProcessors = ProcessorFacades.Num();
		for (int i = 0; i < NumOtherProcessors; i++)
		{
			const TSharedPtr<PCGExData::FFacade> OtherFacade = LocalParent->ProcessorFacades[i];
			if (PointDataFacade == OtherFacade)
			{
				continue;
			} // Skip self

			const TSharedRef<FProcessor> OtherProcessor = StaticCastSharedRef<FProcessor>(*LocalParent->SubProcessorMap->Find(&OtherFacade->Source.Get()));
			const FBox Intersection = Bounds.Overlap(OtherProcessor->GetBounds());

			if (!Intersection.IsValid)
			{
				continue;
			} // No overlap

			RegisterOverlap(&OtherProcessor.Get(), Intersection);
		}

		if (Overlaps.IsEmpty())
		{
			OnRangeProcessingComplete();
			return;
		}

		StartParallelLoopForRange(Overlaps.Num(), 1);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(i)
		{
			ResolveOverlap(i);
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		for (int i = 0; i < NumPoints; i++)
		{
			LocalOverlapSubCountMax = FMath::Max(LocalOverlapSubCountMax, OverlapSubCount[i]);
			LocalOverlapCountMax = FMath::Max(LocalOverlapCountMax, OverlapCount[i]);
		}
	}

	void FProcessor::Write()
	{
		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, SearchTask)

		SearchTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->PointDataFacade->WriteFastest(This->TaskManager);
			PCGExSampling::Helpers::ApplySuccessTags(This->PointDataFacade, This->bAnyOverlap != 0, This->Settings->bTagIfHasAnyOverlap, This->Settings->HasAnyOverlapTag, This->Settings->bTagIfHasNoOverlap, This->Settings->HasNoOverlapTag);
		};

		SearchTask->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->WriteSingleData(Index);
		};

		SearchTask->StartIterations(NumPoints, ParentBatch.Pin()->ProcessorFacades.Num());
	}
}
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
