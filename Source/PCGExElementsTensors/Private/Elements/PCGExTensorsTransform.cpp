// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTensorsTransform.h"

#include "PCGExVersion.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExTensorsTransformElement"
#define PCGEX_NAMESPACE TensorsTransform

TArray<FPCGPinProperties> UPCGExTensorsTransformSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExTensor::SourceTensorsLabel, "Tensors", Required, FPCGExDataTypeInfoTensor::AsId())
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceStopConditionLabel, "Transformed points will be tested against those filters, and transform will stop at first fail. Only a small subset of PCGEx are supported.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(TensorsTransform)

#if WITH_EDITOR
void UPCGExTensorsTransformSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		TensorHandlerDetails.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExTensorsTransformSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		TensorHandlerDetails.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExTensorsTransformSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(TensorsTransform)

bool FPCGExTensorsTransformElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(TensorsTransform)

	if (!PCGExFactories::GetInputFactories(InContext, PCGExTensor::SourceTensorsLabel, Context->TensorFactories, {FPCGExDataTypeInfoTensor::AsId()}))
	{
		return false;
	}

	if (Context->TensorFactories.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing tensors."))
		return false;
	}

	PCGEX_FOREACH_FIELD_TRTENSOR(PCGEX_OUTPUT_VALIDATE_NAME)

	PCGExFactories::GetInputFactories(Context, PCGExFilters::Labels::SourceStopConditionLabel, Context->StopFilterFactories, PCGExFactories::PointFilters(), false);

	PCGExPointFilter::PruneForDirectEvaluation(Context, Context->StopFilterFactories);

	return true;
}

bool FPCGExTensorsTransformElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTensorsTransformElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TensorsTransform)
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
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to subdivide."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExTensorsTransform
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTensorsTransform::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		if (!Context->StopFilterFactories.IsEmpty())
		{
			StopFilters = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!StopFilters->Init(Context, Context->StopFilterFactories))
			{
				StopFilters.Reset();
			}
		}

		TensorsHandler = MakeShared<PCGExTensor::FTensorsHandler>(Settings->TensorHandlerDetails);
		if (!TensorsHandler->Init(Context, Context->TensorFactories, PointDataFacade))
		{
			return false;
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_TRTENSOR(PCGEX_OUTPUT_INIT)
		}

		RemainingIterations = Settings->Iterations;
		Metrics.SetNum(PointDataFacade->GetNum());
		Pings.Init(0, PointDataFacade->GetNum());

		StartParallelLoopForPoints(PCGExData::EIOSide::Out);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::TensorTransform::ProcessPoints);

		if (!bIteratedOnce)
		{
			PointDataFacade->Fetch(Scope);
			FilterScope(Scope);
		}

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			bool bSuccess = false;
			const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(Index, OutTransforms[Index], bSuccess);
			PointFilterCache[Index] = bSuccess;

			if (!bSuccess)
			{
				// TODO : Gracefully stopped
				// TODO : Max iterations not reached
				continue;
			}

			if (StopFilters)
			{
				PCGExData::FProxyPoint ProxyPoint = PCGExData::FProxyPoint(PCGExData::FMutablePoint(PointDataFacade->GetOutPoint(Index))); // Oooof
				if (StopFilters->Test(ProxyPoint))
				{
					PointFilterCache[Index] = false;
					if (Settings->StopConditionHandling == EPCGExTensorStopConditionHandling::Exclude)
					{
						// TODO : Not gracefully stopped
						// TODO : Max iterations not reached
						continue;
					}
				}
			}

			Metrics[Index].Add(OutTransforms[Index].GetLocation());
			Pings[Index] += Sample.Effectors;

			if (Settings->bTransformRotation)
			{
				if (Settings->Rotation == EPCGExTensorTransformMode::Absolute)
				{
					OutTransforms[Index].SetRotation(Sample.Rotation);
				}
				else if (Settings->Rotation == EPCGExTensorTransformMode::Relative)
				{
					OutTransforms[Index].SetRotation(Sample.Rotation * OutTransforms[Index].GetRotation());
				}
				else if (Settings->Rotation == EPCGExTensorTransformMode::Align)
				{
					OutTransforms[Index].SetRotation(PCGExMath::MakeDirection(Settings->AlignAxis, Sample.DirectionAndSize.GetSafeNormal() * -1, OutTransforms[Index].GetRotation().GetUpVector()));
				}
			}

			if (Settings->bTransformPosition)
			{
				OutTransforms[Index].SetLocation(OutTransforms[Index].GetLocation() + Sample.DirectionAndSize);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		bIteratedOnce = true;

		// Drive remaining iterations as a flat while-loop instead of recursing
		// through StartParallelLoopForPoints each iteration.
		const int32 NumPoints = PointDataFacade->GetNum();

		while (--RemainingIterations > 0)
		{
			TArray<PCGExMT::FScope> Loops;
			const int32 NumScopes = PCGExMT::SubLoopScopes(
				Loops, NumPoints,
				FMath::Max(1, PCGExMT::GetSanitizedBatchSize(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize())));

			if (NumScopes == 1)
			{
				ProcessPoints(Loops[0]);
			}
			else
			{
				PCGExMT::ParallelOrSequential(
					NumScopes,
					[this, &Loops](const int32 i) { ProcessPoints(Loops[i]); },
					/*Threshold=*/2, EParallelForFlags::Unbalanced);
			}
		}

		PCGExMT::ParallelOrSequential(
			NumPoints,
			[&](const int32 i)
			{
				const PCGExPaths::FPathMetrics& Metric = Metrics[i];
				const int32 UpdateCount = Metric.Count;

				PCGEX_OUTPUT_VALUE(EffectorsPings, i, Pings[i])
				PCGEX_OUTPUT_VALUE(UpdateCount, i, UpdateCount)
				PCGEX_OUTPUT_VALUE(TraveledDistance, i, Metric.Length)
				PCGEX_OUTPUT_VALUE(GracefullyStopped, i, UpdateCount < Settings->Iterations)
				PCGEX_OUTPUT_VALUE(MaxIterationsReached, i, UpdateCount == Settings->Iterations)
			});

		PointDataFacade->WriteFastest(TaskManager);
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
