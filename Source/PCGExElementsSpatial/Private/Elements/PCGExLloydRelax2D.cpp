// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExLloydRelax2D.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Math/Geo/PCGExGeo.h"

#define LOCTEXT_NAMESPACE "PCGExLloydRelax2DElement"
#define PCGEX_NAMESPACE LloydRelax2D

PCGEX_INITIALIZE_ELEMENT(LloydRelax2D)

#if WITH_EDITOR
void UPCGExLloydRelax2DSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		InfluenceDetails.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExLloydRelax2DSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		InfluenceDetails.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExLloydRelax2DSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(LloydRelax2D)

bool FPCGExLloydRelax2DElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(LloydRelax2D)

	return true;
}

bool FPCGExLloydRelax2DElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExLloydRelax2DElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(LloydRelax2D)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 3 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() <= 3)
				{
					Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			Context->CancelExecution(TEXT("Could not find any points to relax."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExLloydRelax2D
{
	class FLloydRelaxTask final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FLloydRelaxTask(const int32 InTaskIndex, const TSharedPtr<FProcessor>& InProcessor, const FPCGExInfluenceDetails* InInfluenceSettings, const int32 InNumIterations)
			: FPCGExIndexedTask(InTaskIndex)
			  , Processor(InProcessor)
			  , InfluenceSettings(InInfluenceSettings)
			  , NumIterations(InNumIterations)
		{
		}

		TSharedPtr<FProcessor> Processor;
		const FPCGExInfluenceDetails* InfluenceSettings = nullptr;
		int32 NumIterations = 0;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			NumIterations--;

			TUniquePtr<PCGExMath::Geo::TDelaunay2> Delaunay = MakeUnique<PCGExMath::Geo::TDelaunay2>();
			TArray<FVector>& Positions = Processor->ActivePositions;

			const TArrayView<FVector> View = MakeArrayView(Positions);
			// Only Sites are consumed below; skip the DelaunayEdges set and hull extraction.
			if (!Delaunay->Process(View, Processor->ProjectionDetails, false, false))
			{
				return;
			}

			const int32 NumPoints = Positions.Num();

			TArray<FVector> Sum;
			TArray<double> Counts;
			Sum.Append(Processor->ActivePositions);
			Counts.SetNum(NumPoints);
			for (int i = 0; i < NumPoints; i++)
			{
				Counts[i] = 1;
			}

			FVector Centroid;
			for (const PCGExMath::Geo::FDelaunaySite2& Site : Delaunay->Sites)
			{
				PCGExMath::Geo::GetCentroid(Positions, Site.Vtx, Centroid);
				for (const int32 PtIndex : Site.Vtx)
				{
					Counts[PtIndex] += 1;
					Sum[PtIndex] += Centroid;
				}
			}

			if (InfluenceSettings->bProgressiveInfluence)
			{
				PCGEX_PARALLEL_FOR(
					NumPoints,
					Positions[i] = FMath::Lerp(Positions[i], Sum[i] / Counts[i], InfluenceSettings->GetInfluence(i));
					)
			}

			Delaunay.Reset();

			if (NumIterations > 0)
			{
				PCGEX_LAUNCH_INTERNAL(FLloydRelaxTask, TaskIndex + 1, Processor, InfluenceSettings, NumIterations)
			}
		}
	};

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLloydRelax2D::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		ProjectionDetails = Settings->ProjectionDetails;
		ProjectionDetails.Init(PointDataFacade);

		InfluenceDetails = Settings->InfluenceDetails;
		if (!InfluenceDetails.Init(ExecutionContext, PointDataFacade))
		{
			return false;
		}

		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->GetIn(), ActivePositions);

		PCGEX_SHARED_THIS_DECL
		PCGEX_LAUNCH(FLloydRelaxTask, 0, ThisPtr, &InfluenceDetails, Settings->Iterations)

		return true;
	}

	void FProcessor::CompleteWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::LloydRelax2D::CompleteWork);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);

		if (InfluenceDetails.bProgressiveInfluence)
		{
			PCGExMT::ParallelOrSequential(
				OutTransforms.Num(),
				[&](const int32 i)
				{
					FTransform& Transform = OutTransforms[i];

					FVector TargetPosition = Transform.GetLocation();
					TargetPosition.X = ActivePositions[i].X;
					TargetPosition.Y = ActivePositions[i].Y;

					Transform.SetLocation(TargetPosition);
				});
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				OutTransforms.Num(),
				[&](const int32 i)
				{
					FTransform& Transform = OutTransforms[i];

					FVector TargetPosition = Transform.GetLocation();
					TargetPosition.X = ActivePositions[i].X;
					TargetPosition.Y = ActivePositions[i].Y;

					Transform.SetLocation(FMath::Lerp(Transform.GetLocation(), TargetPosition, InfluenceDetails.GetInfluence(i)));
				});
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
