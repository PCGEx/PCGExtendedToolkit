// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFuseCollinear.h"

#include "Blenders/PCGExUnionBlender.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/PCGExPathShift.h"
#include "Math/PCGExMathDistances.h"
#include "Paths/PCGExPath.h"
#include "Sampling/PCGExSamplingUnionData.h"


#define LOCTEXT_NAMESPACE "PCGExFuseCollinearElement"
#define PCGEX_NAMESPACE FuseCollinear

PCGEX_INITIALIZE_ELEMENT(FuseCollinear)
PCGEX_ELEMENT_BATCH_POINT_IMPL(FuseCollinear)

bool FPCGExFuseCollinearElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FuseCollinear)

	Context->DotThreshold = PCGExMath::DegreesToDot(Settings->Threshold);
	Context->FuseDistSquared = FMath::Square(Settings->FuseDistance);

	if (!Settings->UnionDetails.SanityCheck(Context))
	{
		return false;
	}

	return true;
}


bool FPCGExFuseCollinearElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFuseCollinearElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FuseCollinear)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					if (!Settings->bOmitInvalidPathsFromOutput)
					{
						Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					}
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			Context->CancelExecution(TEXT("Could not find any paths to fuse."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExFuseCollinear
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFuseCollinear::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Path = MakeShared<PCGExPaths::FPath>(PointDataFacade->GetIn(), 0);


		TArray<int32> ReadIndices;
		ReadIndices.Reserve(Path->NumPoints);
		LastPosition = Path->GetPos(0);

		bForceSingleThreadedProcessPoints = true;
		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		FilterAll();

		// Preserve start & end
		PointFilterCache[0] = true;

		// Only force-preserve last point if not closed loop
		if (!Path->IsClosedLoop())
		{
			PointFilterCache[Path->LastIndex] = true;
		}

		for (int i = 0; i < Path->NumPoints; i++)
		{
			if (PointFilterCache[i])
			{
				// Kept point, as per filters
				ReadIndices.Add(i);
				LastPosition = Path->GetPos(i);
				continue;
			}

			const FVector CurrentPos = Path->GetPos(i);
			if (Settings->bFuseCollocated && FVector::DistSquared(LastPosition, CurrentPos) <= Context->FuseDistSquared)
			{
				// Collocated points
				continue;
			}

			// Use last position to avoid removing smooth arcs
			const double Dot = FVector::DotProduct((CurrentPos - LastPosition).GetSafeNormal(), Path->DirToNextPoint(i));
			if ((!Settings->bInvertThreshold && Dot > Context->DotThreshold) || (Settings->bInvertThreshold && Dot < Context->DotThreshold))
			{
				// Collinear with previous, keep moving
				continue;
			}

			ReadIndices.Add(i);
			LastPosition = Path->GetPos(i);
		}

		if (ReadIndices.Num() < 2)
		{
			return false;
		}

		if (Path->IsClosedLoop())
		{
			// Make sure the first point isn't collinear with next valid/last valid
			const int32 NextIndex = ReadIndices[1];
			const int32 LastIndex = ReadIndices.Last();

			const FVector FirstPos = Path->GetPos(0);
			const FVector ForwardDir = (Path->GetPos(NextIndex) - FirstPos).GetSafeNormal();
			const FVector WrapDir = (FirstPos - Path->GetPos(LastIndex)).GetSafeNormal();

			// Use last position to avoid removing smooth arcs
			const double Dot = FVector::DotProduct(WrapDir, ForwardDir);
			if ((!Settings->bInvertThreshold && Dot > Context->DotThreshold) || (Settings->bInvertThreshold && Dot < Context->DotThreshold))
			{
				// Collinear with previous
				ReadIndices.RemoveAt(0);
			}
		}

		if (ReadIndices.Num() < 2)
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointDataFacade->GetOut(), ReadIndices.Num(), PointDataFacade->GetAllocations());
		PointDataFacade->Source->InheritPoints(ReadIndices, 0);

		Finalize(ReadIndices);

		return true;
	}

	void FProcessor::Finalize(TArray<int32>& ReadIndices)
	{
		TSharedPtr<PCGExBlending::FUnionBlender> DataBlender = nullptr;

		if (Settings->UnionDetails.bWriteIsUnion)
		{
			IsUnionWriter = PointDataFacade->GetWritable<bool>(
				Settings->UnionDetails.IsUnionAttributeName,
				false, true, PCGExData::EBufferInit::New);
		}

		if (Settings->UnionDetails.bWriteUnionSize)
		{
			UnionSizeWriter = PointDataFacade->GetWritable<int32>(
				Settings->UnionDetails.UnionSizeAttributeName,
				1, true, PCGExData::EBufferInit::New);
		}

		if (!Settings->bDoBlend && !Settings->UnionDetails.WriteAny())
		{
			return;
		}

		const int32 NumInPoints = PointDataFacade->GetNum();
		const int32 LastRead = ReadIndices.Num() - 1;

		if (!Settings->bDoBlend)
		{
			for (int i = 0; i < LastRead; i++)
			{
				const int32 From = ReadIndices[i];
				const int32 To = ReadIndices[i + 1];
				const int32 Count = To - From;

				if (Count <= 1)
				{
					continue;
				}

				if (IsUnionWriter)
				{
					IsUnionWriter->SetValue(i, true);
				}
				if (UnionSizeWriter)
				{
					UnionSizeWriter->SetValue(i, Count);
				}
			}

			// Last point
			const int32 Count = (NumInPoints - ReadIndices.Last()) + ReadIndices[0];
			if (Count > 1)
			{
				if (IsUnionWriter)
				{
					IsUnionWriter->SetValue(LastRead, true);
				}
				if (UnionSizeWriter)
				{
					UnionSizeWriter->SetValue(LastRead, Count);
				}
			}

			PointDataFacade->WriteFastest(TaskManager);

			return;
		}


		DataBlender = MakeShared<PCGExBlending::FUnionBlender>(
			const_cast<FPCGExBlendingDetails*>(&Settings->BlendingDetails),
			nullptr, PCGExMath::GetNoneDistances());

		TArray<TSharedRef<PCGExData::FFacade>> UnionSources;
		UnionSources.Add(PointDataFacade);

		TSet<FName> ProtectedAttributes;
		if (Settings->UnionDetails.bWriteIsUnion)
		{
			ProtectedAttributes.Add(Settings->UnionDetails.IsUnionAttributeName);
		}
		if (Settings->UnionDetails.bWriteUnionSize)
		{
			ProtectedAttributes.Add(Settings->UnionDetails.UnionSizeAttributeName);
		}

		DataBlender->AddSources(UnionSources, &ProtectedAttributes);

		if (!DataBlender->Init(Context, PointDataFacade))
		{
			return;
		}

		TArray<PCGExData::FWeightedPoint> OutWeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		DataBlender->InitTrackers(Trackers);

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();

		const int32 IOIndex = PointDataFacade->Source->IOIndex;

		for (int i = 0; i < LastRead; i++)
		{
			const int32 From = ReadIndices[i];
			const int32 To = ReadIndices[i + 1];
			const int32 Count = To - From;

			if (Count <= 1)
			{
				continue;
			}

			if (IsUnionWriter)
			{
				IsUnionWriter->SetValue(i, true);
			}
			if (UnionSizeWriter)
			{
				UnionSizeWriter->SetValue(i, Count);
			}

			Union->Reset();
			Union->Reserve(1, Count);

			for (int j = 0; j < Count; j++)
			{
				Union->AddWeighted_Unsafe(PCGExData::FElement(From + j, IOIndex), 1);
			}

			DataBlender->ComputeWeights(i, Union, OutWeightedPoints);
			DataBlender->Blend(i, OutWeightedPoints, Trackers);
		}

		{
			const int32 From = ReadIndices.Last();
			const int32 Count = (NumInPoints - From) + ReadIndices[0];

			if (Count > 1)
			{
				if (IsUnionWriter)
				{
					IsUnionWriter->SetValue(LastRead, true);
				}
				if (UnionSizeWriter)
				{
					UnionSizeWriter->SetValue(LastRead, Count);
				}

				Union->Reset();
				Union->Reserve(1, Count);

				for (int j = 0; j < Count; j++)
				{
					Union->AddWeighted_Unsafe(PCGExData::FElement((From + j) % NumInPoints, IOIndex), 1);
				}

				DataBlender->ComputeWeights(LastRead, Union, OutWeightedPoints);
				DataBlender->Blend(LastRead, OutWeightedPoints, Trackers);
			}
		}

		PointDataFacade->WriteFastest(TaskManager);
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
