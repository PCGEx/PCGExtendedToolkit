// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExOrient.h"


#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Orient/PCGExOrientAverage.h"
#include "Elements/Orient/PCGExOrientLookAt.h"

#define LOCTEXT_NAMESPACE "PCGExOrientElement"
#define PCGEX_NAMESPACE Orient

#if WITH_EDITORONLY_DATA
void UPCGExOrientSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Orientation) { Orientation = NewObject<UPCGExOrientLookAt>(this, TEXT("Orientation")); }
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExOrientSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExOrient::SourceOverridesOrient)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(Orient)

PCGExData::EIOInit UPCGExOrientSettings::GetMainDataInitializationPolicy() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }

PCGEX_ELEMENT_BATCH_POINT_IMPL(Orient)

bool FPCGExOrientElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(Orient)

	if (!Settings->Orientation)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Please select an orientation module in the detail panel."));
		return false;
	}

	if (Settings->Output == EPCGExOrientUsage::OutputToAttribute) { PCGEX_VALIDATE_NAME(Settings->OutputAttribute); }
	if (Settings->bOutputDot) { PCGEX_VALIDATE_NAME(Settings->DotAttribute); }

	PCGEX_BIND_INSTANCED_FACTORY(Orientation, UPCGExOrientInstancedFactory, PCGExOrient::SourceOverridesOrient)
	Context->Orientation->OrientAxis = Settings->OrientAxis;
	Context->Orientation->UpAxis = Settings->UpAxis;

	return true;
}

bool FPCGExOrientElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExOrientElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(Orient)
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
			Context->CancelExecution(TEXT("Could not find any paths to orient."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExOrient
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExOrient::Process);

		DefaultPointFilterValue = Settings->bFlipDirection;

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())
		PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		Path = MakeShared<PCGExPaths::FPath>(PointDataFacade->GetIn(), 0);
		//PathBinormal = Path->AddExtra<PCGExPaths::FPathEdgeBinormal>(false);

		LastIndex = PointDataFacade->GetNum() - 1;

		Orient = Context->Orientation->CreateOperation();
		if (!Orient->PrepareForData(PointDataFacade, Path.ToSharedRef())) { return false; }

		if (Settings->Output == EPCGExOrientUsage::OutputToAttribute)
		{
			TransformWriter = PointDataFacade->GetWritable<FTransform>(Settings->OutputAttribute, PCGExData::EBufferInit::Inherit);
		}

		if (Settings->bOutputDot)
		{
			DotWriter = PointDataFacade->GetWritable<double>(Settings->DotAttribute, PCGExData::EBufferInit::Inherit);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::Orient::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		TPCGValueRange<FTransform> OutTransform = PointDataFacade->GetOut()->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (Path->IsValidEdgeIndex(Index)) { Path->ComputeEdgeExtra(Index); }

			const FTransform OutT = Orient->ComputeOrientation(PointDataFacade->GetOutPoint(Index), PointFilterCache[Index] ? -1 : 1);
			if (Settings->bOutputDot) { DotWriter->SetValue(Index, FVector::DotProduct(Path->DirToPrevPoint(Index) * -1, Path->DirToNextPoint(Index))); }

			if (TransformWriter) { TransformWriter->SetValue(Index, OutT); }
			else { OutTransform[Index] = OutT; }
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
