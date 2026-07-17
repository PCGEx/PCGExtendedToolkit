// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExLloydRelax.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExInfluenceDetails.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Math/Geo/PCGExGeo.h"

#define LOCTEXT_NAMESPACE "PCGExLloydRelaxElement"
#define PCGEX_NAMESPACE LloydRelax

PCGEX_INITIALIZE_ELEMENT(LloydRelax)

#if WITH_EDITOR
void UPCGExLloydRelaxSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		InfluenceDetails.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExLloydRelaxSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		InfluenceDetails.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExLloydRelaxSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(LloydRelax)

bool FPCGExLloydRelaxElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(LloydRelax)

	return true;
}

bool FPCGExLloydRelaxElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExLloydRelaxElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(LloydRelax)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 4 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() <= 4)
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
			return Context->CancelExecution(TEXT("Could not find any points to relax."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExLloydRelax
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

			TUniquePtr<PCGExMath::Geo::TDelaunay3> Delaunay = MakeUnique<PCGExMath::Geo::TDelaunay3>();
			TArray<FVector>& Positions = Processor->ActivePositions;

			const TArrayView<FVector> View = MakeArrayView(Positions);
			if (!Delaunay->Process<false, false>(View))
			{
				return;
			}

			const int32 NumPoints = Positions.Num();

			TArray<FVector> Sum;
			Sum.Append(Processor->ActivePositions);

			TArray<double> Counts;
			Counts.Init(1, NumPoints);

			FVector Centroid;
			for (const PCGExMath::Geo::FDelaunaySite3& Site : Delaunay->Sites)
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
				PCGEX_PARALLEL_FOR(NumPoints, Positions[i] = FMath::Lerp(Positions[i], Sum[i] / Counts[i], InfluenceSettings->GetInfluence(i));)
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
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLloydRelax::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

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
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::LloydRelax::CompleteWork);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);

		if (InfluenceDetails.bProgressiveInfluence)
		{
			PCGEX_PARALLEL_FOR(
				OutTransforms.Num(),
				OutTransforms[i].SetLocation(ActivePositions[i]);
				)
		}
		else
		{
			PCGEX_PARALLEL_FOR(
				OutTransforms.Num(),
				FTransform& Transform = OutTransforms[i];
				Transform.SetLocation(FMath::Lerp(Transform.GetLocation(), ActivePositions[i], InfluenceDetails.GetInfluence(i)));
				)
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
