// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleSockets.h"

#include "PCGExVersion.h"
#include "PCGComponent.h"
#include "Engine/StaticMesh.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExAssetLoader.h"
#include "Helpers/PCGExSocketHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Metadata/PCGObjectPropertyOverride.h"

#include "Paths/PCGExPath.h"

#define LOCTEXT_NAMESPACE "PCGExSampleSocketsElement"
#define PCGEX_NAMESPACE BuildCustomGraph

#if WITH_EDITOR
void UPCGExSampleSocketsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(TEXT("AssetPathAttributeName")), FName(TEXT("Asset")), FName(TEXT("Attribute")), FName(TEXT(" └─ Asset (Attr)")));
		PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(TEXT("StaticMesh")), FName(TEXT("Asset")), FName(TEXT("Constant")), FName(TEXT(" └─ Asset")));
		RetireInputPin(InOutNode, FName(TEXT("AssetType")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleSocketsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Asset.Update(AssetType_DEPRECATED, AssetPathAttributeName_DEPRECATED, StaticMesh_DEPRECATED.ToSoftObjectPath());
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_INITIALIZE_ELEMENT(SampleSockets)

TArray<FPCGPinProperties> UPCGExSampleSocketsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExStaging::Labels::OutputSocketLabel, "Socket points.", Normal)
	return PinProperties;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleSockets)

void FPCGExSampleSocketsContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();
	if (StaticMeshLoader) { StaticMeshLoader->AddAssetDependencies(); }
}

bool FPCGExSampleSocketsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleSockets)

	PCGEX_FWD(OutputSocketDetails)
	if (!Context->OutputSocketDetails.Init(Context))
	{
		return false;
	}

	if (Settings->Asset.Input == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_C(Context, Settings->Asset.Attribute)
		if (Settings->Asset.bCleanupAttribute) { Context->AddConsumableAttributeName(Settings->Asset.Attribute); }

		TArray<FName> Names = {Settings->Asset.Attribute};
		Context->StaticMeshLoader = MakeShared<PCGEx::TAssetLoader<UStaticMesh>>(Context, Context->MainPoints, Names);
		if (!Context->StaticMeshLoader->Discover())
		{
			return Context->CancelExecution(TEXT("Failed to find any asset to load."));
		}
	}
	else
	{
		const TSoftObjectPtr<UStaticMesh> StaticMeshPtr(Settings->Asset.Constant);
		PCGExHelpers::LoadBlocking_AnyThreadTpl(StaticMeshPtr, Context);
		Context->StaticMesh = StaticMeshPtr.Get();
		if (!Context->StaticMesh)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Static mesh could not be loaded."));
			return false;
		}
	}

	Context->SocketsCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->SocketsCollection->OutputPin = PCGExStaging::Labels::OutputSocketLabel;

	return true;
}

void FPCGExSampleSocketsElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(SampleSockets)
	if (Context->StaticMeshLoader)
	{
		Context->StaticMeshLoader->Finalize();
	}
}

bool FPCGExSampleSocketsElement::PostBoot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::PostBoot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(SampleSockets)
	if (Context->StaticMeshLoader && Context->StaticMeshLoader->IsEmpty())
	{
		return InContext->CancelExecution(TEXT("Failed to load any assets."));
	}
	return true;
}

bool FPCGExSampleSocketsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleSocketsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleSockets)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to write tangents to."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->SocketsCollection->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleSockets
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->Asset.Input == EPCGExInputValueType::Attribute)
		{
			Keys = Context->StaticMeshLoader->GetKeys(PointDataFacade->Source->IOIndex);
		}

		SocketHelper = MakeShared<PCGExStaging::FSocketHelper>(&Context->OutputSocketDetails, PointDataFacade->GetNum());

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleSockets::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		const TArray<PCGExValueHash>& KeysRef = Keys ? *Keys.Get() : TArray<PCGExValueHash>{};

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const TObjectPtr<UStaticMesh>* SM = Keys ? Context->StaticMeshLoader->GetAsset(KeysRef[Index]) : &Context->StaticMesh;
			if (!SM)
			{
				continue;
			}
			SocketHelper->Add(Index, *SM);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		SocketHelper->Compile(TaskManager, PointDataFacade, Context->SocketsCollection);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
