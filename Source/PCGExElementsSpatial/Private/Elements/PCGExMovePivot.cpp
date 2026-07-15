// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExMovePivot.h"

#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExMovePivotElement"
#define PCGEX_NAMESPACE MovePivot

PCGEX_INITIALIZE_ELEMENT(MovePivot)

#if WITH_EDITOR
void UPCGExMovePivotSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		UVW.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExMovePivotSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		UVW.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExMovePivotSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(MovePivot)

bool FPCGExMovePivotElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(MovePivot)

	return true;
}

bool FPCGExMovePivotElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExMovePivotElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(MovePivot)
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
			return Context->CancelExecution(TEXT("No data."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExMovePivot
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExMovePivot::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		UVW = Settings->UVW;
		if (!UVW.Init(ExecutionContext, PointDataFacade))
		{
			return false;
		}

		// Cherry pick native properties allocations

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		AllocateFor |= EPCGPointNativeProperties::BoundsMin;
		AllocateFor |= EPCGPointNativeProperties::BoundsMax;
		AllocateFor |= EPCGPointNativeProperties::Transform;

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::MovePivot::ProcessPoints);

		UPCGBasePointData* OutPoints = PointDataFacade->GetOut();
		TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange(false);
		TPCGValueRange<FVector> OutBoundsMin = OutPoints->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> OutBoundsMax = OutPoints->GetBoundsMaxValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			FVector Offset;
			OutTransforms[Index].SetLocation(UVW.GetPosition(Index, Offset));
			OutBoundsMin[Index] += Offset;
			OutBoundsMax[Index] += Offset;
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
