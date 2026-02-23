// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWriteTangents.h"


#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Tangents/PCGExTangentsAuto.h"

#define LOCTEXT_NAMESPACE "PCGExWriteTangentsElement"
#define PCGEX_NAMESPACE BuildCustomGraph

PCGEX_SETTING_VALUE_IMPL(UPCGExWriteTangentsSettings, ArriveScale, FVector, ArriveScaleInput, ArriveScaleAttribute, FVector(ArriveScaleConstant))
PCGEX_SETTING_VALUE_IMPL(UPCGExWriteTangentsSettings, LeaveScale, FVector, LeaveScaleInput, LeaveScaleAttribute, FVector(LeaveScaleConstant))

#if WITH_EDITORONLY_DATA
void UPCGExWriteTangentsSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Tangents) { Tangents = NewObject<UPCGExAutoTangents>(this, TEXT("Tangents")); }
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExWriteTangentsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExTangents::SourceOverridesTangents)
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExTangents::SourceOverridesTangentsStart)
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExTangents::SourceOverridesTangentsEnd)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(WriteTangents)

PCGExData::EIOInit UPCGExWriteTangentsSettings::GetMainDataInitializationPolicy() const { return PCGExData::EIOInit::Duplicate; }

PCGEX_ELEMENT_BATCH_POINT_IMPL(WriteTangents)

FName UPCGExWriteTangentsSettings::GetPointFilterPin() const
{
	return PCGExFilters::Labels::SourcePointFiltersLabel;
}

UPCGExWriteTangentsSettings::UPCGExWriteTangentsSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	if (ArriveScaleAttribute.GetName() == FName("@Last")) { ArriveScaleAttribute.Update(TEXT("$Scale")); }
	if (LeaveScaleAttribute.GetName() == FName("@Last")) { LeaveScaleAttribute.Update(TEXT("$Scale")); }
#endif
}

bool FPCGExWriteTangentsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(WriteTangents)

	PCGEX_VALIDATE_NAME(Settings->ArriveName)
	PCGEX_VALIDATE_NAME(Settings->LeaveName)

	PCGEX_BIND_INSTANCED_FACTORY(Tangents, UPCGExTangentsInstancedFactory, PCGExTangents::SourceOverridesTangents)
	if (Settings->StartTangents) { PCGEX_BIND_INSTANCED_FACTORY(StartTangents, UPCGExTangentsInstancedFactory, PCGExTangents::SourceOverridesTangentsStart) }
	if (Settings->EndTangents) { PCGEX_BIND_INSTANCED_FACTORY(EndTangents, UPCGExTangentsInstancedFactory, PCGExTangents::SourceOverridesTangentsEnd) }

	return true;
}


bool FPCGExWriteTangentsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteTangentsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WriteTangents)
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
					Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to write tangents to."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExWriteTangents
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn());

		Tangents = Context->Tangents->CreateOperation();
		Tangents->bClosedLoop = bClosedLoop;

		if (!Tangents->PrepareForData(Context)) { return false; }

		ArriveScaleReader = Settings->GetValueSettingArriveScale();
		if (!ArriveScaleReader->Init(PointDataFacade)) { return false; }

		LeaveScaleReader = Settings->GetValueSettingLeaveScale();
		if (!LeaveScaleReader->Init(PointDataFacade)) { return false; }

		if (Context->StartTangents)
		{
			StartTangents = Context->StartTangents->CreateOperation();
			StartTangents->bClosedLoop = bClosedLoop;
			StartTangents->PrimaryDataFacade = PointDataFacade;

			if (!StartTangents->PrepareForData(Context)) { return false; }
		}
		else { StartTangents = Tangents; }

		if (Context->EndTangents)
		{
			EndTangents = Context->EndTangents->CreateOperation();
			EndTangents->bClosedLoop = bClosedLoop;
			EndTangents->PrimaryDataFacade = PointDataFacade;

			if (!EndTangents->PrepareForData(Context)) { return false; }
		}
		else { EndTangents = Tangents; }

		ArriveWriter = PointDataFacade->GetWritable(Settings->ArriveName, FVector::ZeroVector, true, PCGExData::EBufferInit::Inherit);
		LeaveWriter = PointDataFacade->GetWritable(Settings->LeaveName, FVector::ZeroVector, true, PCGExData::EBufferInit::Inherit);

		LastIndex = PointDataFacade->GetNum() - 1;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::WriteTangents::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		const UPCGBasePointData* InPoints = PointDataFacade->Source->GetIn();

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			int32 PrevIndex = Index - 1;
			int32 NextIndex = Index + 1;

			FVector OutArrive = FVector::ZeroVector;
			FVector OutLeave = FVector::ZeroVector;

			const FVector& ArriveScale = ArriveScaleReader->Read(Index);
			const FVector& LeaveScale = LeaveScaleReader->Read(Index);

			if (bClosedLoop)
			{
				if (PrevIndex < 0) { PrevIndex = LastIndex; }
				if (NextIndex > LastIndex) { NextIndex = 0; }

				Tangents->ProcessPoint(InPoints, Index, NextIndex, PrevIndex, ArriveScale, OutArrive, LeaveScale, OutLeave);
			}
			else
			{
				if (Index == 0)
				{
					StartTangents->ProcessFirstPoint(InPoints, ArriveScale, OutArrive, LeaveScale, OutLeave);
				}
				else if (Index == LastIndex)
				{
					EndTangents->ProcessLastPoint(InPoints, ArriveScale, OutArrive, LeaveScale, OutLeave);
				}
				else
				{
					Tangents->ProcessPoint(InPoints, Index, NextIndex, PrevIndex, ArriveScale, OutArrive, LeaveScale, OutLeave);
				}
			}

			ArriveWriter->SetValue(Index, OutArrive);
			LeaveWriter->SetValue(Index, OutLeave);
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
