// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFindClustersData.h"

#include "Clusters/PCGExClusterDataLibrary.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExFindClustersDataElement"
#define PCGEX_NAMESPACE BuildCustomGraph

#if WITH_EDITOR
bool UPCGExFindClustersDataSettings::GetCompactNodeIcon(FName& OutCompactNodeIcon) const
{
	OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
	return true;
}
#endif

TArray<FPCGPinProperties> UPCGExFindClustersDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainInputPin(), "The point data to be processed.", Required)
	if (SearchMode != EPCGExClusterDataSearchMode::All)
	{
		PCGEX_PIN_POINT(GetSearchOutputPin(), "The search data to match against.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindClustersDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discarded data.", Advanced)
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindClustersDataSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindClustersData)

bool FPCGExFindClustersDataElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindClustersData)

	if (Settings->SearchMode != EPCGExClusterDataSearchMode::All)
	{
		Context->SearchKeyIO = PCGExData::TryGetSingleInput(Context, Settings->GetSearchOutputPin(), false, true);
		if (!Context->SearchKeyIO)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid reference input."));
			return false;
		}

		if (Settings->SearchMode == EPCGExClusterDataSearchMode::EdgesFromVtx)
		{
			if (!Context->SearchKeyIO->Tags->IsTagged(PCGExClusters::Labels::TagStr_PCGExVtx))
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid reference input (not a Vtx group)."));
				return false;
			}
		}
		else if (Settings->SearchMode == EPCGExClusterDataSearchMode::VtxFromEdges)
		{
			if (!Context->SearchKeyIO->Tags->IsTagged(PCGExClusters::Labels::TagStr_PCGExEdges))
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid reference input. (not an Edges group)"));
				return false;
			}
		}

		Context->SearchKey = PCGEX_GET_DATAIDTAG(Context->SearchKeyIO->Tags, PCGExClusters::Labels::TagStr_PCGExCluster);
		if (!Context->SearchKey)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Found no valid key to match against."));
			return false;
		}

		if (!Context->MainPoints->ContainsData_Unsafe(Context->SearchKeyIO->GetIn()))
		{
			// Add Search key to main points so the library can be rebuilt as expected
			Context->MainPoints->Add_Unsafe(Context->SearchKeyIO);
		}
	}

	return true;
}


bool FPCGExFindClustersDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindClustersDataElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindClustersData)

	if (!Boot(Context))
	{
		return true;
	}

	const TSharedPtr<PCGExClusters::FDataLibrary> Library = MakeShared<PCGExClusters::FDataLibrary>(true);
	const bool bValidLibrary = Library->Build(Context->MainPoints);

	// Prints on success too, so partial problems surface (and the skip toggles always apply).
	Library->PrintLogs(Context, Settings->bSkipTrivialWarnings, Settings->bSkipImportantWarnings);

	if (!bValidLibrary)
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Could not find any valid vtx/edge pairs."))
		return Context->CancelExecution();
	}

	if (Settings->SearchMode == EPCGExClusterDataSearchMode::All)
	{
		for (const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries : Library->InputDictionary->Entries)
		{
			if (!Entries.IsValid())
			{
				continue;
			}

			Entries->Key->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;
			Entries->Key->InitializeOutput(PCGExData::EIOInit::Forward);

			for (const TSharedRef<PCGExData::FPointIO>& EdgeIO : Entries->Entries)
			{
				EdgeIO->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;
				EdgeIO->InitializeOutput(PCGExData::EIOInit::Forward);
			}
		}
	}
	else
	{
		TSharedPtr<PCGExData::FPointIOTaggedEntries> EdgeEntries = Library->InputDictionary->GetEntries(Context->SearchKey->Value);
		if (!EdgeEntries || EdgeEntries->Entries.IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Could not find any match."));
			return true;
		}

		if (Settings->SearchMode == EPCGExClusterDataSearchMode::EdgesFromVtx)
		{
			Context->SearchKeyIO->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;
			Context->SearchKeyIO->InitializeOutput(PCGExData::EIOInit::Forward);

			for (const TSharedRef<PCGExData::FPointIO>& EdgeIO : EdgeEntries->Entries)
			{
				EdgeIO->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;
				EdgeIO->InitializeOutput(PCGExData::EIOInit::Forward);
			}
		}
		else
		{
			Context->SearchKeyIO->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;
			Context->SearchKeyIO->InitializeOutput(PCGExData::EIOInit::Forward);

			EdgeEntries->Key->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;
			EdgeEntries->Key->InitializeOutput(PCGExData::EIOInit::Forward);
		}
	}

	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		if (!IO->IsEnabled())
		{
			IO->Enable();
			IO->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;
			IO->InitializeOutput(PCGExData::EIOInit::Forward);
		}
	}

	Context->MainPoints->StageOutputs();

	Context->Done();

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
