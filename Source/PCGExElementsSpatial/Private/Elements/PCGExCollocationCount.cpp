// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCollocationCount.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExCollocationCountElement"
#define PCGEX_NAMESPACE CollocationCount

PCGEX_INITIALIZE_ELEMENT(CollocationCount)

PCGExData::EIOInit UPCGExCollocationCountSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(CollocationCount)

bool FPCGExCollocationCountElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CollocationCount)

	PCGEX_VALIDATE_NAME(Settings->CollicationNumAttributeName)
	if (Settings->bWriteLinearOccurences)
	{
		PCGEX_VALIDATE_NAME(Settings->LinearOccurencesAttributeName)
	}

	return true;
}

bool FPCGExCollocationCountElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCollocationCountElement::Execute);

	PCGEX_CONTEXT(CollocationCount)
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

	return Context->TryComplete();
}

namespace PCGExCollocationCount
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCollocationCount::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		ToleranceConstant = Settings->Tolerance;

		CollocationWriter = PointDataFacade->GetWritable(Settings->CollicationNumAttributeName, 0, true, PCGExData::EBufferInit::New);

		if (Settings->bWriteLinearOccurences)
		{
			LinearOccurencesWriter = PointDataFacade->GetWritable(Settings->LinearOccurencesAttributeName, 0, true, PCGExData::EBufferInit::New);
		}

		Octree = &PointDataFacade->Source->GetIn()->GetPointOctree();

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::CollocationCount::ProcessPoints);

		TConstPCGValueRange<FTransform> Transforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			const FVector Center = Transforms[Index].GetLocation();
			const double Tolerance = ToleranceConstant;

			CollocationWriter->SetValue(Index, 0);

			if (LinearOccurencesWriter)
			{
				LinearOccurencesWriter->SetValue(Index, 0);
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Center, FVector(Tolerance)), [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (PointRef.Index == Index)
					{
						return;
					}
					if (FVector::Dist(Center, Transforms[PointRef.Index].GetLocation()) > Tolerance)
					{
						return;
					}

					CollocationWriter->SetValue(Index, 1);

					if (PointRef.Index < Index)
					{
						LinearOccurencesWriter->SetValue(Index, 1);
					}
				});
			}
			else
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Center, FVector(Tolerance)), [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (PointRef.Index == Index)
					{
						return;
					}
					if (FVector::Dist(Center, Transforms[PointRef.Index].GetLocation()) > Tolerance)
					{
						return;
					}

					CollocationWriter->SetValue(Index, 1);
				});
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
