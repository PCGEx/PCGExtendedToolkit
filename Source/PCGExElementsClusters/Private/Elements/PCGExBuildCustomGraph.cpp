// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildCustomGraph.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "PCGComponent.h"

#include "PCGParamData.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"

#include "Graphs/PCGExGraph.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "Graphs/PCGExGraphBuilder.h"

#define LOCTEXT_NAMESPACE "PCGExBuildCustomGraphElement"
#define PCGEX_NAMESPACE BuildCustomGraph

void UPCGExCustomGraphBuilder::Initialize_Implementation(bool& OutSuccess)
{
	OutSuccess = false;
}

void UPCGExCustomGraphSettings::InitializeSettings_Implementation(bool& OutSuccess, int32& OutNodeReserve, int32& OutEdgeReserve)
{
}

int32 UPCGExCustomGraphSettings::GetOrCreateNode(const int64 InIdx)
{
	if (const int32* IndexPtr = IdxMap.Find(InIdx)) { return *IndexPtr; }
	const int32 Index = Idx.Add(InIdx);
	IdxMap.Add(InIdx, Index);
	return Index;
}

void UPCGExCustomGraphSettings::AddEdge(const int64 InStartIdx, const int64 InEndIdx)
{
	if (InStartIdx == InEndIdx) { return; }
	UniqueEdges.Add(PCGEx::H64U(GetOrCreateNode(InStartIdx), GetOrCreateNode(InEndIdx)));
}

void UPCGExCustomGraphSettings::RemoveEdge(const int64 InStartIdx, const int64 InEndIdx)
{
	if (InStartIdx == InEndIdx) { return; }
	UniqueEdges.Remove(PCGEx::H64U(GetOrCreateNode(InStartIdx), GetOrCreateNode(InEndIdx)));
}

void UPCGExCustomGraphSettings::InitPointAttributes_Implementation(bool& OutSuccess)
{
	OutSuccess = true;
}

void UPCGExCustomGraphSettings::BuildGraph_Implementation(bool& OutSuccess)
{
	OutSuccess = false;
}

void UPCGExCustomGraphSettings::UpdateNodePoint_Implementation(const FPCGPoint& InPoint, const int64 InNodeIdx, const int32 InPointIndex, FPCGPoint& OutPoint)
{
	OutPoint = InPoint;
}

// Convenient macro to avoid duplicating a lot of code with all our supported types.
#define PCGEX_FOREACH_ATTR_TYPE(MACRO) \
MACRO(Int32, int32) \
MACRO(Int64, int64) \
MACRO(Float, float) \
MACRO(Double, double) \
MACRO(Vector2, FVector2D) \
MACRO(Vector, FVector) \
MACRO(Vector4, FVector4) \
MACRO(Quat, FQuat) \
MACRO(Transform, FTransform) \
MACRO(String, FString) \
MACRO(Bool, bool) \
MACRO(Rotator, FRotator) \
MACRO(Name, FName)

#pragma region Node Attributes

#define PCGEX_SET_ATT_IMPL(_NAME, _TYPE)\
bool UPCGExCustomGraphSettings::InitNode##_NAME(const FName& InAttributeName, const _TYPE& InValue){\
TSharedPtr<PCGExData::TBuffer<_TYPE>> Buffer = VtxBuffers->GetBuffer<_TYPE>(InAttributeName, InValue);\
return Buffer ? true : false;}
PCGEX_FOREACH_ATTR_TYPE(PCGEX_SET_ATT_IMPL)
#undef PCGEX_SET_ATT_IMPL

bool UPCGExCustomGraphSettings::InitNodeSoftObjectPath(const FName& InAttributeName, const FSoftObjectPath& InValue)
{
	TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> Buffer = VtxBuffers->GetBuffer<FSoftObjectPath>(InAttributeName, InValue);
	return Buffer ? true : false;
}

bool UPCGExCustomGraphSettings::InitNodeSoftClassPath(const FName& InAttributeName, const FSoftClassPath& InValue)
{
	TSharedPtr<PCGExData::TBuffer<FSoftClassPath>> Buffer = VtxBuffers->GetBuffer<FSoftClassPath>(InAttributeName, InValue);
	return Buffer ? true : false;
}

#define PCGEX_SET_ATT_IMPL(_NAME, _TYPE)\
bool UPCGExCustomGraphSettings::SetNode##_NAME(const FName& InAttributeName, const int64 InNodeID, const _TYPE& InValue){\
return VtxBuffers->SetValue<_TYPE>(InAttributeName, IdxMap[InNodeID], InValue);}
PCGEX_FOREACH_ATTR_TYPE(PCGEX_SET_ATT_IMPL)
#undef PCGEX_SET_ATT_IMPL

bool UPCGExCustomGraphSettings::SetNodeSoftObjectPath(const FName& InAttributeName, const int64 InNodeID, const FSoftObjectPath& InValue)
{
	return VtxBuffers->SetValue<FSoftObjectPath>(InAttributeName, IdxMap[InNodeID], InValue);
}

bool UPCGExCustomGraphSettings::SetNodeSoftClassPath(const FName& InAttributeName, const int64 InNodeID, const FSoftClassPath& InValue)
{
	return VtxBuffers->SetValue<FSoftClassPath>(InAttributeName, IdxMap[InNodeID], InValue);
}

#pragma endregion

#undef PCGEX_FOREACH_ATTR_TYPE


void UPCGExCustomGraphBuilder::CreateGraphSettings(TSubclassOf<UPCGExCustomGraphSettings> SettingsClass, UPCGExCustomGraphSettings*& OutSettings)
{
	if (!SettingsClass || SettingsClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogPCGEx, Error, TEXT("Cannot instantiate an abstract class!"));
		return;
	}

	TObjectPtr<UPCGExCustomGraphSettings> NewSettings = Context->ManagedObjects->New<UPCGExCustomGraphSettings>(GetTransientPackage(), SettingsClass.Get());
	NewSettings->SettingsIndex = GraphSettings.Add(NewSettings);
	OutSettings = NewSettings;
}

void UPCGExCustomGraphBuilder::BuildGraph_Implementation(UPCGExCustomGraphSettings* InCustomGraphSettings, bool& OutSuccess)
{
	InCustomGraphSettings->BuildGraph(OutSuccess);
}

namespace PCGExBuildCustomGraph
{
	class FBuildGraph final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FBuildGraph)

		FBuildGraph(const TSharedPtr<PCGExData::FPointIO>& InPointIO, UPCGExCustomGraphSettings* InGraphSettings)
			: FTask(), PointIO(InPointIO), GraphSettings(InGraphSettings)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		UPCGExCustomGraphSettings* GraphSettings = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			FPCGExBuildCustomGraphContext* Context = TaskManager->GetContext<FPCGExBuildCustomGraphContext>();
			PCGEX_SETTINGS(BuildCustomGraph)

			UPCGExCustomGraphBuilder* Builder = Context->Builder;

			bool bInitSuccess = false;
			int32 NodeReserveNum = 0;
			int32 EdgeReserveNum = 0;

			{
				if (!IsInGameThread())
				{
					FGCScopeGuard Scope;
					GraphSettings->InitializeSettings(bInitSuccess, NodeReserveNum, EdgeReserveNum);
				}
				else
				{
					GraphSettings->InitializeSettings(bInitSuccess, NodeReserveNum, EdgeReserveNum);
				}
			}

			if (!bInitSuccess)
			{
				if (!Settings->bQuietUnprocessedSettingsWarning)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A graph builder settings has less than 2 max nodes and won't be processed."));
				}

				PCGEX_CLEAR_IO_VOID(PointIO)
				return;
			}

			if (NodeReserveNum > 0)
			{
				GraphSettings->Idx.Reserve(NodeReserveNum);
				GraphSettings->IdxMap.Reserve(NodeReserveNum);
			}

			if (EdgeReserveNum > 0)
			{
				GraphSettings->UniqueEdges.Reserve(EdgeReserveNum);
			}
			else if (NodeReserveNum > 0)
			{
				GraphSettings->UniqueEdges.Reserve(NodeReserveNum * 3); // Wild guess
			}

			bool bSuccessfulBuild = false;
			Builder->BuildGraph(GraphSettings, bSuccessfulBuild);

			if (!bSuccessfulBuild)
			{
				if (!Settings->bQuietFailedBuildGraphWarning)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A graph builder 'BuildGraph' returned false."));
				}
				return;
			}

			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointIO->GetOut(), GraphSettings->Idx.Num());

			PCGEX_MAKE_SHARED(NodeDataFacade, PCGExData::FFacade, PointIO.ToSharedRef())
			PCGEX_MAKE_SHARED(GraphBuilder, PCGExGraphs::FGraphBuilder, NodeDataFacade.ToSharedRef(), &Settings->GraphBuilderDetails)
			GraphBuilder->OutputNodeIndices = MakeShared<TArray<int32>>();

			GraphSettings->VtxBuffers = MakeShared<PCGExData::TBufferHelper<PCGExData::EBufferHelperMode::Write>>(NodeDataFacade.ToSharedRef());
			GraphSettings->GraphBuilder = GraphBuilder;

			GraphBuilder->Graph->InsertEdges(GraphSettings->UniqueEdges, -1);

			bool bSuccessfulAttrInit = false;

			{
				if (!IsInGameThread())
				{
					FGCScopeGuard Scope;
					GraphSettings->InitPointAttributes(bSuccessfulAttrInit);
				}
				else
				{
					GraphSettings->InitPointAttributes(bSuccessfulAttrInit);
				}
			}

			if (!bSuccessfulAttrInit)
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("A graph builder 'InitPointAttributes' returned false."));
				GraphSettings->GraphBuilder->bCompiledSuccessfully = false;
				return;
			}

			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, InitNodesGroup)

			TWeakPtr<PCGExData::FPointIO> WeakIO = PointIO;
			TWeakPtr<PCGExGraphs::FGraphBuilder> WeakGraphBuilder = GraphBuilder;

			InitNodesGroup->OnCompleteCallback = [WeakGraphBuilder, TaskManager]()
			{
				const TSharedPtr<PCGExGraphs::FGraphBuilder> GBuilder = WeakGraphBuilder.Pin();
				if (!GBuilder) { return; }

				GBuilder->CompileAsync(TaskManager, true);
			};

			UPCGExCustomGraphSettings* CustomGraphSettings = GraphSettings;
			InitNodesGroup->OnSubLoopStartCallback = [WeakIO, CustomGraphSettings](const PCGExMT::FScope& Scope)
			{
				const TSharedPtr<PCGExData::FPointIO> IO = WeakIO.Pin();
				if (!IO) { return; }

				TArray<FPCGPoint> MutablePoints;
				GetPoints(IO->GetOutScope(Scope), MutablePoints);

				PCGEX_SCOPE_LOOP(i)
				{
					FPCGPoint& Point = MutablePoints[i];
					CustomGraphSettings->UpdateNodePoint(Point, CustomGraphSettings->Idx[i], i, Point);
				}

				IO->SetPoints(Scope.Start, MutablePoints);
			};

			PointIO->GetOutKeys(true); // Generate out keys		
			InitNodesGroup->StartSubLoops(CustomGraphSettings->Idx.Num(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);
		}
	};
}

TArray<FPCGPinProperties> UPCGExBuildCustomGraphSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBuildCustomGraph::SourceOverridesBuilder)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBuildCustomGraphSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BuildCustomGraph)

bool FPCGExBuildCustomGraphElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BuildCustomGraph)

	if (!Settings->Builder)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No builder selected."));
		return false;
	}

	Context->EDITOR_TrackClass(Settings->Builder->GetClass());

	PCGEX_BIND_INSTANCED_FACTORY(Builder, UPCGExCustomGraphBuilder, PCGExBuildCustomGraph::SourceOverridesBuilder)

	if (Settings->Mode == EPCGExCustomGraphActorSourceMode::ActorReferences)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->ActorReferenceAttribute)
	}

	return true;
}

bool FPCGExBuildCustomGraphElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildCustomGraphElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildCustomGraph)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (Settings->Mode == EPCGExCustomGraphActorSourceMode::Owner)
		{
			Context->Builder->InputActors.Add(Context->GetComponent()->GetOwner());
		}
		else
		{
			while (Context->AdvancePointsIO())
			{
				TUniquePtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> ActorReferences = MakeUnique<PCGExData::TAttributeBroadcaster<FSoftObjectPath>>();
				if (!ActorReferences->Prepare(Settings->ActorReferenceAttribute, Context->CurrentIO.ToSharedRef()))
				{
					PCGE_LOG(Warning, GraphAndLog, FTEXT("Some inputs don't have the specified Actor Reference attribute."));
				}

				ActorReferences->Grab();
				TSet<AActor*> UniqueActors;

				for (FSoftObjectPath Path : ActorReferences->Values)
				{
					if (AActor* SourceActor = Cast<AActor>(Path.ResolveObject())) { UniqueActors.Add(SourceActor); }
				}

				Context->Builder->InputActors.Reserve(UniqueActors.Num());
				Context->Builder->InputActors.Append(UniqueActors.Array());
			}
		}

		// Init builder now that we have resolved actor references.

		bool bSuccessfulInit = false;

		{
			if (!IsInGameThread())
			{
				FGCScopeGuard Scope;
				Context->Builder->Initialize(bSuccessfulInit);
			}
			else
			{
				Context->Builder->Initialize(bSuccessfulInit);
			}
		}

		if (!bSuccessfulInit)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Builder returned failed initialization."));
			return true;
		}

		// Prepare graph builders
		if (Context->Builder->GraphSettings.IsEmpty())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Builder generated no graph settings."));
			return true;
		}

		Context->SetState(PCGExGraphs::States::State_WritingClusters);

		TSet<UClass*> UniqueSettingsClasses;

		for (UPCGExCustomGraphSettings* GraphSettings : Context->Builder->GraphSettings)
		{
			bool bAlreadySet = false;
			UniqueSettingsClasses.Add(GraphSettings->GetClass(), &bAlreadySet);
			if (!bAlreadySet) { Context->EDITOR_TrackClass(GraphSettings->GetClass()); }

			TSharedPtr<PCGExData::FPointIO> NodeIO = Context->MainPoints->Emplace_GetRef();
			NodeIO->IOIndex = GraphSettings->SettingsIndex;

			{
				const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
				PCGEX_LAUNCH(PCGExBuildCustomGraph::FBuildGraph, NodeIO, GraphSettings)
			}
		}

		return false;
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExGraphs::States::State_WritingClusters)
	{
		for (UPCGExCustomGraphSettings* GraphSettings : Context->Builder->GraphSettings)
		{
			if (!GraphSettings->GraphBuilder) { continue; }
			if (GraphSettings->GraphBuilder->bCompiledSuccessfully)
			{
				GraphSettings->GraphBuilder->StageEdgesOutputs();
			}
			else
			{
				// Invalidate node IO
				GraphSettings->GraphBuilder->NodeDataFacade->Source->InitializeOutput(PCGExData::EIOInit::NoInit);
			}
		}

		Context->MainPoints->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
