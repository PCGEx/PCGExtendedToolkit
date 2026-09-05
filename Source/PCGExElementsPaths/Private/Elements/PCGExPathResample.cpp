// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathResample.h"


#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Paths/PCGExPath.h"

#define LOCTEXT_NAMESPACE "PCGExResamplePathElement"
#define PCGEX_NAMESPACE ResamplePath

#if WITH_EDITOR
void UPCGExResamplePathSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameOutputPin(FName("Resolution"), FName("Constant"));
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExResamplePathSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 71, 3)
	{
		SampleLength.Constant = Resolution_DEPRECATED;
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_INITIALIZE_ELEMENT(ResamplePath)

PCGExData::EIOInit UPCGExResamplePathSettings::GetMainDataInitializationPolicy() const
{
	if (Mode == EPCGExResampleMode::Sweep)
	{
		return PCGExData::EIOInit::New;
	}
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(ResamplePath)

bool FPCGExResamplePathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ResamplePath)

	return true;
}

bool FPCGExResamplePathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExResamplePathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ResamplePath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid path."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExResamplePath
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExResamplePath::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = false; //Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const UPCGBasePointData* InPoints = PointDataFacade->GetIn();

		bPreserveLastPoint = Settings->bPreserveLastPoint;
		bAutoSampleSize = true;

		if (!Settings->SampleLength.TryReadDataValue(PointDataFacade->Source, SampleLength))
		{
			return false;
		}

		Path = MakeShared<PCGExPaths::FPath>(InPoints, 0);
		Path->IOIndex = PointDataFacade->Source->IOIndex;
		PathLength = Path->AddExtra<PCGExPaths::FPathEdgeLength>(true); // Force compute length

		if (Settings->Mode == EPCGExResampleMode::Sweep)
		{
			if (Settings->ResolutionMode == EPCGExResolutionMode::Fixed)
			{
				NumSamples = SampleLength;
				bAutoSampleSize = true;
			}
			else
			{
				NumSamples = PCGExMath::TruncateDbl(PathLength->TotalLength / SampleLength, Settings->Truncate);
				bAutoSampleSize = Settings->bRedistributeEvenly;
			}


			if (NumSamples < 2)
			{
				return false;
			}

			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointDataFacade->GetOut(), NumSamples, PointDataFacade->GetAllocations() | EPCGPointNativeProperties::Seed);
		}
		else
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
			PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);
			NumSamples = PointDataFacade->GetNum();
		}

		if (Path->IsClosedLoop())
		{
			NumSamples++;
		}

		if (bAutoSampleSize)
		{
			bPreserveLastPoint = false;
			SampleLength = PathLength->TotalLength / static_cast<double>(NumSamples - 1);
		}

		Samples.SetNumUninitialized(NumSamples);
		bForceSingleThreadedProcessPoints = true;

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		int32 StartIndex = 0;
		int32 EndIndex = 1;
		FVector PrevPosition = InTransforms[0].GetLocation();
		double TraversedDistance = 0;

		FPointSample& FirstSample = Samples[0];
		FirstSample.Start = StartIndex;
		FirstSample.End = EndIndex;
		FirstSample.Location = PrevPosition;
		FirstSample.Distance = TraversedDistance;

		for (int i = 1; i < NumSamples; i++)
		{
			FPointSample& Sample = Samples[i];
			Sample.Start = StartIndex;

			FVector NextPosition = InTransforms[EndIndex].GetLocation();
			double DistToNext = FVector::Dist(PrevPosition, NextPosition);
			double Remainder = SampleLength - DistToNext;

			if (Remainder <= 0)
			{
				// Overshooting
				PrevPosition = PrevPosition + (Path->DirToNextPoint(StartIndex) * SampleLength);
				TraversedDistance += SampleLength;
			}
			else
			{
				PrevPosition = NextPosition;

				while (Remainder > 0)
				{
					StartIndex = EndIndex++;

					if (EndIndex >= InTransforms.Num())
					{
						if (!Path->IsClosedLoop())
						{
							EndIndex = InTransforms.Num() - 1;
							break;
						}

						EndIndex = 0;
					}

					NextPosition = InTransforms[EndIndex].GetLocation();
					DistToNext = FVector::Dist(PrevPosition, NextPosition);

					if (Remainder <= DistToNext)
					{
						PrevPosition = PrevPosition + (Path->DirToPrevPoint(EndIndex) * -Remainder);
					}
					else
					{
						PrevPosition = NextPosition;
					}
					Remainder -= DistToNext;
				}
			}

			Sample.End = EndIndex;
			Sample.Location = PrevPosition;
			Sample.Distance = TraversedDistance;
		}

		if (bPreserveLastPoint && !Path->IsClosedLoop())
		{
			FPointSample& LastSample = Samples.Last();
			LastSample.Start = InTransforms.Num() - 2;
			LastSample.End = InTransforms.Num() - 1;
			LastSample.Location = InTransforms[LastSample.End].GetLocation();
			LastSample.Distance = TraversedDistance;
		}

		if (Settings->Mode == EPCGExResampleMode::Sweep)
		{
			// Blender will take care of setting all the properties and stuff			
			MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
			MetadataBlender->SetSourceData(PointDataFacade);
			MetadataBlender->SetTargetData(PointDataFacade);
			if (!MetadataBlender->Init(Context, Settings->BlendingSettings, nullptr, PCGExData::EProxyFlags::None, PCGExData::EIOSide::In))
			{
				return false;
			}
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::ResamplePath::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeed = PointDataFacade->GetOut()->GetSeedValueRange(false);

		if (Settings->Mode == EPCGExResampleMode::Redistribute)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				const FPointSample& Sample = Samples[Index];
				OutTransforms[Index].SetLocation(Sample.Location);
				if (Settings->bEnsureUniqueSeeds)
				{
					OutSeed[Index] = PCGExRandomHelpers::ComputeSpatialSeed(Sample.Location);
				}
			}
		}
		else
		{
			TArray<PCGEx::FOpStats> Trackers;
			MetadataBlender->InitTrackers(Trackers);

			PCGEX_SCOPE_LOOP(Index)
			{
				const FPointSample& Sample = Samples[Index];

				OutTransforms[Index].SetLocation(Sample.Location);

				if (Settings->bEnsureUniqueSeeds)
				{
					OutSeed[Index] = PCGExRandomHelpers::ComputeSpatialSeed(Sample.Location);
				}

				const FVector Start = Path->GetPos(Sample.Start);
				const double SampleBreadth = FVector::Dist(Start, Path->GetPos(Sample.End));

				const double Weight = SampleBreadth > 0 ? FVector::Dist(Start, Sample.Location) / SampleBreadth : 0.5;
				MetadataBlender->Blend(Sample.Start, Sample.End, Index, Weight);

				// TODO : complex blending across the whole SourcesRange
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
