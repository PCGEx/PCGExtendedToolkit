// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadSockets.h"

#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExSocketStagingElement"
#define PCGEX_NAMESPACE SocketStaging

PCGEX_INITIALIZE_ELEMENT(SocketStaging)

void UPCGExSocketStagingSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	Super::InputPinPropertiesBeforeFilters(PinProperties);
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SocketStaging)

TArray<FPCGPinProperties> UPCGExSocketStagingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExStaging::Labels::OutputSocketLabel, "Socket points.", Normal)
	return PinProperties;
}

bool FPCGExSocketStagingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(SocketStaging)

	Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionPickUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	PCGEX_FWD(OutputSocketDetails)
	if (!Context->OutputSocketDetails.Init(Context)) { return false; }

	Context->SocketsCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->SocketsCollection->OutputPin = PCGExStaging::Labels::OutputSocketLabel;

	return true;
}

bool FPCGExSocketStagingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSocketStagingElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SocketStaging)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	Context->SocketsCollection->StageOutputs();
	return Context->TryComplete();
}

namespace PCGExSocketStaging
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSocketStaging::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter) { return false; }
		
		SocketHelper = MakeShared<PCGExCollections::FSocketHelper>(&Context->OutputSocketDetails, PointDataFacade->GetNum());

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SocketStaging::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		int16 MaterialPick = 0;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
				Result.IsValid())
			{
				SocketHelper->Add(Index, PCGExStaging::GetSimplifiedEntryHash(Hash), Result.Entry);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		SocketHelper->Compile(TaskManager, PointDataFacade, Context->SocketsCollection);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
