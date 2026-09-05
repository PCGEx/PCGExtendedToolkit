// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDiscardByOverlap.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/PCGExMathBounds.h"


#define LOCTEXT_NAMESPACE "PCGExDiscardByOverlapElement"
#define PCGEX_NAMESPACE DiscardByOverlap

void FPCGExOverlapScoresWeighting::Init()
{
	StaticWeightSum = FMath::Abs(NumPoints) + FMath::Abs(Volume) + FMath::Abs(VolumeDensity) + FMath::Abs(CustomTagScore) + FMath::Abs(DataScore);
	NumPoints /= StaticWeightSum;
	Volume /= StaticWeightSum;
	VolumeDensity /= StaticWeightSum;
	CustomTagWeight /= StaticWeightSum;
	DataScoreWeight /= StaticWeightSum;

	DynamicWeightSum = FMath::Abs(OverlapCount) + FMath::Abs(OverlapSubCount) + FMath::Abs(OverlapVolume) + FMath::Abs(OverlapVolumeDensity);
	OverlapCount /= DynamicWeightSum;
	OverlapSubCount /= DynamicWeightSum;
	OverlapVolume /= DynamicWeightSum;
	OverlapVolumeDensity /= DynamicWeightSum;

	const double Balance = FMath::Abs(DynamicBalance) + FMath::Abs(StaticBalance);
	DynamicBalance /= Balance;
	StaticBalance /= Balance;
}

void FPCGExOverlapScoresWeighting::ResetMin()
{
	OverlapCount = OverlapSubCount = OverlapVolume = OverlapVolumeDensity = NumPoints = Volume = VolumeDensity = CustomTagScore = DataScore = TNumericLimits<double>::Min();
}

void FPCGExOverlapScoresWeighting::Max(const FPCGExOverlapScoresWeighting& Other)
{
	OverlapCount = FMath::Max(OverlapCount, Other.OverlapCount);
	OverlapSubCount = FMath::Max(OverlapSubCount, Other.OverlapSubCount);
	OverlapVolume = FMath::Max(OverlapVolume, Other.OverlapVolume);
	OverlapVolumeDensity = FMath::Max(OverlapVolumeDensity, Other.OverlapVolumeDensity);
	NumPoints = FMath::Max(NumPoints, Other.NumPoints);
	Volume = FMath::Max(Volume, Other.Volume);
	VolumeDensity = FMath::Max(VolumeDensity, Other.VolumeDensity);
	CustomTagScore = FMath::Max(CustomTagScore, Other.CustomTagScore);
	DataScore = FMath::Max(DataScore, Other.DataScore);
}

TSharedPtr<PCGExDiscardByOverlap::FOverlap> FPCGExDiscardByOverlapContext::RegisterOverlap(PCGExDiscardByOverlap::FProcessor* InA, PCGExDiscardByOverlap::FProcessor* InB, const FBox& InIntersection)
{
	const uint64 HashID = PCGEx::H64U(InA->BatchIndex, InB->BatchIndex);

	{
		FReadScopeLock ReadScopeLock(OverlapLock);
		if (TSharedPtr<PCGExDiscardByOverlap::FOverlap>* FoundPtr = OverlapMap.Find(HashID))
		{
			return *FoundPtr;
		}
	}

	{
		FWriteScopeLock WriteScopeLock(OverlapLock);
		if (TSharedPtr<PCGExDiscardByOverlap::FOverlap>* FoundPtr = OverlapMap.Find(HashID))
		{
			return *FoundPtr;
		}

		const bool bInvert = InA->BatchIndex > InB->BatchIndex;
		PCGEX_MAKE_SHARED(NewOverlap, PCGExDiscardByOverlap::FOverlap, bInvert ? InB : InA, bInvert ? InA : InB, InIntersection)
		OverlapMap.Add(HashID, NewOverlap);
		return NewOverlap;
	}
}

void FPCGExDiscardByOverlapContext::UpdateScores(const TArray<PCGExDiscardByOverlap::FProcessor*>& InStack)
{
	MaxScores.ResetMin();
	for (const PCGExDiscardByOverlap::FProcessor* C : InStack)
	{
		MaxScores.Max(C->RawScores);
	}
	for (PCGExDiscardByOverlap::FProcessor* C : InStack)
	{
		C->UpdateWeight(MaxScores);
	}
}

void FPCGExDiscardByOverlapContext::Prune()
{
	PCGEX_SETTINGS_LOCAL(DiscardByOverlap)

	TArray<PCGExDiscardByOverlap::FProcessor*> OverlapsStack;
	OverlapsStack.Reserve(MainBatch->GetNumProcessors());

	for (const TPair<PCGExData::FPointIO*, TSharedPtr<PCGExPointsMT::IProcessor>> Pair : SubProcessorMap)
	{
		PCGExDiscardByOverlap::FProcessor* P = static_cast<PCGExDiscardByOverlap::FProcessor*>(Pair.Value.Get());
		if (!P->bIsProcessorValid)
		{
			continue;
		}
		if (P->HasOverlaps())
		{
			OverlapsStack.Add(P);
		}
		else
		{
			PCGEX_INIT_IO_VOID(P->PointDataFacade->Source, PCGExData::EIOInit::Forward)
		}
	}

	UpdateScores(OverlapsStack);

	while (!OverlapsStack.IsEmpty())
	{
		// Sort remaining overlaps...

		if (Settings->Logic == EPCGExOverlapPruningLogic::LowFirst)
		{
			OverlapsStack.Sort([](const PCGExDiscardByOverlap::FProcessor& A, const PCGExDiscardByOverlap::FProcessor& B)
			{
				return A.Weight == B.Weight ? A.PointDataFacade->Source->IOIndex > B.PointDataFacade->Source->IOIndex : A.Weight > B.Weight;
			});
		}
		else
		{
			OverlapsStack.Sort([](const PCGExDiscardByOverlap::FProcessor& A, const PCGExDiscardByOverlap::FProcessor& B)
			{
				return A.Weight == B.Weight ? A.PointDataFacade->Source->IOIndex > B.PointDataFacade->Source->IOIndex : A.Weight < B.Weight;
			});
		}

		PCGExDiscardByOverlap::FProcessor* Candidate = OverlapsStack.Pop();

		if (Candidate->HasOverlaps())
		{
			Candidate->Pruned(OverlapsStack);
		}
		else
		{
			PCGEX_INIT_IO_VOID(Candidate->PointDataFacade->Source, PCGExData::EIOInit::Forward)
		}

		UpdateScores(OverlapsStack);
	}
}

PCGEX_INITIALIZE_ELEMENT(DiscardByOverlap)
PCGEX_ELEMENT_BATCH_POINT_IMPL(DiscardByOverlap)

bool FPCGExDiscardByOverlapElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}


	PCGEX_CONTEXT_AND_SETTINGS(DiscardByOverlap)

	Context->Weights = Settings->Weighting;

	if (Settings->TestMode == EPCGExOverlapTestMode::Fast)
	{
		Context->Weights.DynamicBalance = 0;
		Context->Weights.StaticBalance = 1;
	}

	Context->Weights.Init();

	if (Context->MainPoints->Num() < 2)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Not enough inputs; requires at least 2 to check for overlap."));
		return false;
	}

	return true;
}

bool FPCGExDiscardByOverlapElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDiscardByOverlapElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(DiscardByOverlap)
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
				NewBatch->bRequiresWriteStep = true; // Not really but we need the step
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any input to check for overlaps."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->Prune();
	Context->Done();

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExDiscardByOverlap
{
	FOverlap::FOverlap(FProcessor* InManager, FProcessor* InManaged, const FBox& InIntersection)
		: Intersection(InIntersection)
		  , Manager(InManager)
		  , Managed(InManaged)
	{
		HashID = PCGEx::H64U(InManager->BatchIndex, InManaged->BatchIndex);
	}

	void FProcessor::RegisterOverlap(FProcessor* InOtherProcessor, const FBox& Intersection)
	{
		FWriteScopeLock WriteScopeLock(RegistrationLock);
		const TSharedPtr<FOverlap> Overlap = Context->RegisterOverlap(this, InOtherProcessor, Intersection);
		if (Overlap->Manager == this)
		{
			ManagedOverlaps.Add(Overlap);
		}
		Overlaps.Add(Overlap);
	}

	void FProcessor::RemoveOverlap(const TSharedPtr<FOverlap>& InOverlap, TArray<FProcessor*>& RemaininStack)
	{
		Overlaps.Remove(InOverlap);

		if (Overlaps.IsEmpty())
		{
			PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			RemaininStack.Remove(this);
			return;
		}

		Stats.Remove(InOverlap->Stats, NumPoints, TotalVolume);
		UpdateWeightValues();
	}

	void FProcessor::Pruned(TArray<FProcessor*>& RemaininStack)
	{
		// Remove self from the stack
		for (const TSharedPtr<FOverlap>& Overlap : Overlaps)
		{
			Overlap->GetOther(this)->RemoveOverlap(Overlap, RemaininStack);
		}

		Overlaps.Empty();
	}

	void FProcessor::RegisterPointBounds(const int32 Index, const TSharedPtr<FPointBounds>& InPointBounds)
	{
		const int8 bValidPoint = PointFilterCache[Index];
		if (!bValidPoint && !Settings->bIncludeFilteredInMetrics)
		{
			return;
		}

		const FBox& B = InPointBounds->Bounds.GetBox();
		Bounds += B;
		TotalVolume += B.GetVolume();

		if (bValidPoint)
		{
			LocalPointBounds[Index] = InPointBounds;
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// 1 - Build bounds & octrees

		InPoints = PointDataFacade->GetIn();
		NumPoints = InPoints->GetNumPoints();

		PCGExArrayHelpers::InitArray(LocalPointBounds, NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

#define PCGEX_PREPARE_BOUNDS(_BOUND_SOURCE) \
		if (Settings->BoundsSource == EPCGExPointBoundsSource::_BOUND_SOURCE){\
			PCGEX_SCOPE_LOOP(i){\
				const PCGExData::FConstPoint Point(InPoints, i);\
				const FBox LocalBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::_BOUND_SOURCE>(Point).ExpandBy(Settings->Expansion);\
				TSharedPtr<PCGExDiscardByOverlap::FPointBounds> PtBounds = MakeShared<PCGExDiscardByOverlap::FPointBounds>(i, Point, LocalBounds);\
				RegisterPointBounds(i, PtBounds); } }

		PCGEX_PREPARE_BOUNDS(ScaledBounds)
		else
			PCGEX_PREPARE_BOUNDS(DensityBounds)
			else
				PCGEX_PREPARE_BOUNDS(Bounds)
				else
					PCGEX_PREPARE_BOUNDS(Center)

#undef PCGEX_PREPARE_BOUNDS
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		const TConstPCGValueRange<float> Densities = InPoints->GetConstDensityValueRange();

		Octree = MakeUnique<FPointBoundsOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length());
		for (const TSharedPtr<FPointBounds>& PtBounds : LocalPointBounds)
		{
			if (!PtBounds)
			{
				continue;
			}
			Octree->AddElement(PtBounds.Get());
			TotalDensity += Densities[PtBounds->Index];
		}

		VolumeDensity = NumPoints / TotalVolume;
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

		if (Settings->TestMode == EPCGExOverlapTestMode::Fast)
		{
			for (const TSharedPtr<FOverlap>& Overlap : Overlaps)
			{
				Overlap->Stats.OverlapCount = 1;
				Overlap->Stats.OverlapVolume = Overlap->Intersection.GetVolume();
			}
		}
		else
		{
			// Require one more expensive step...
			if (!ManagedOverlaps.IsEmpty())
			{
				StartParallelLoopForRange(ManagedOverlaps.Num(), 1);
			}
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPoints->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			// For each managed overlap, find per-point intersections

			const TSharedPtr<FOverlap> Overlap = ManagedOverlaps[Index];
			const TSharedRef<FProcessor> OtherProcessor = StaticCastSharedRef<FProcessor>(*ParentBatch.Pin()->SubProcessorMap->Find(&Overlap->GetOther(this)->PointDataFacade->Source.Get()));


			if (Settings->TestMode != EPCGExOverlapTestMode::Sphere)
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Overlap->Intersection.GetCenter(), Overlap->Intersection.GetExtent()), [&](const FPointBounds* LocalPoint)
				{
					const double Length = LocalPoint->LocalBounds.GetExtent().Length() * 2;
					const FMatrix InvMatrix = InTransforms[LocalPoint->Index].ToMatrixNoScale().Inverse();

					OtherProcessor->GetOctree()->FindElementsWithBoundsTest(FBoxCenterAndExtent(LocalPoint->Bounds.GetBox()), [&](const FPointBounds* OtherPoint)
					{
						const FBox Intersection = LocalPoint->LocalBounds.Overlap(OtherPoint->TransposedBounds(InvMatrix));

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

						Overlap->Stats.OverlapCount++;
						Overlap->Stats.OverlapVolume += Intersection.GetVolume();
					});
				});
			}
			else
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Overlap->Intersection.GetCenter(), Overlap->Intersection.GetExtent()), [&](const FPointBounds* OwnedPoint)
				{
					const FSphere S1 = OwnedPoint->Bounds.GetSphere();

					OtherProcessor->GetOctree()->FindElementsWithBoundsTest(FBoxCenterAndExtent(OwnedPoint->Bounds.GetBox()), [&](const FPointBounds* OtherPoint)
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
						else
						{
							if (Amount < Settings->MinThreshold)
							{
								return;
							}
						}

						Overlap->Stats.OverlapCount++;
						Overlap->Stats.OverlapVolume += Amount;
					});
				});
			}
		}
	}

	void FProcessor::Write()
	{
		ManagedOverlaps.Empty();

		// Sanitize stats & overlaps
		int32 WriteIndex = 0;
		for (int i = 0; i < Overlaps.Num(); i++)
		{
			const TSharedPtr<FOverlap> Overlap = Overlaps[i];
			if (!Overlap->Stats.OverlapCount)
			{
				continue;
			}

			Stats.Add(Overlap->Stats);
			Overlaps[WriteIndex++] = Overlap;
		}

		Overlaps.SetNum(WriteIndex);

		Stats.UpdateRelative(NumPoints, TotalVolume);

		RawScores.NumPoints = static_cast<double>(NumPoints);
		RawScores.Volume = TotalVolume;
		RawScores.VolumeDensity = VolumeDensity;

		RawScores.CustomTagScore = 0;
		RawScores.DataScore = 0;

		for (const TPair<FString, double>& TagScore : Settings->Weighting.TagScores)
		{
			if (PointDataFacade->Source->Tags->IsTagged(TagScore.Key))
			{
				RawScores.CustomTagScore += TagScore.Value;
			}
		}

		for (const FName Name : Settings->Weighting.DataScores)
		{
			double S = 0;
			PCGExData::Helpers::TryReadDataValue(Context, PointDataFacade->GetIn(), Name, S);
			RawScores.DataScore += S;
		}

		UpdateWeightValues();
	}

	void FProcessor::UpdateWeightValues()
	{
		RawScores.OverlapCount = static_cast<double>(Overlaps.Num());
		RawScores.OverlapSubCount = static_cast<double>(Stats.OverlapCount);
		RawScores.OverlapVolume = Stats.OverlapVolume;
		RawScores.OverlapVolumeDensity = Stats.OverlapVolumeAvg;
	}

	void FProcessor::UpdateWeight(const FPCGExOverlapScoresWeighting& InMax)
	{
		const FPCGExOverlapScoresWeighting& W = Context->Weights;

		StaticWeight = 0;
		StaticWeight += (RawScores.NumPoints / InMax.NumPoints) * W.NumPoints;
		StaticWeight += (RawScores.Volume / InMax.Volume) * W.Volume;
		StaticWeight += (RawScores.VolumeDensity / InMax.VolumeDensity) * W.VolumeDensity;
		StaticWeight += (RawScores.CustomTagScore / InMax.CustomTagScore) * W.CustomTagWeight;
		StaticWeight += (RawScores.DataScore / InMax.DataScore) * W.DataScoreWeight;

		DynamicWeight = 0;
		DynamicWeight += (RawScores.OverlapCount / InMax.OverlapCount) * W.OverlapCount;
		DynamicWeight += (RawScores.OverlapSubCount / InMax.OverlapSubCount) * W.OverlapSubCount;
		DynamicWeight += (RawScores.OverlapVolume / InMax.OverlapVolume) * W.OverlapVolume;
		DynamicWeight += (RawScores.OverlapVolumeDensity / InMax.OverlapVolumeDensity) * W.OverlapVolumeDensity;

		Weight = StaticWeight * W.StaticBalance + DynamicWeight * W.DynamicBalance;
	}

#if WITH_EDITOR
	void FProcessor::PrintWeights() const
	{
		UE_LOG(LogTemp, Warning, TEXT("Set #%d | W = %f | SW = %f | DW = %f | NumPoints = %f, Volume = %f, VolumeDensity = %f, OverlapCount = %f, OverlapSubCount = %f, OverlapVolume = %f, OverlapVolumeDensity = %f"), BatchIndex, Weight, StaticWeight, DynamicWeight, RawScores.NumPoints, RawScores.Volume, RawScores.VolumeDensity, RawScores.OverlapCount, RawScores.OverlapSubCount, RawScores.OverlapVolume, RawScores.OverlapVolumeDensity)
	}
#endif
}
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
