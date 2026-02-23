// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWaitForPCGData.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Helpers/PCGExStreamingHelpers.h"
#include "PCGGraph.h"
#include "PCGSubsystem.h"
#include "PCGExSubSystem.h"
#include "Tasks/Task.h"
#include "Async/Async.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Utils/PCGExIntTracker.h"

#define LOCTEXT_NAMESPACE "PCGExWaitForPCGDataElement"
#define PCGEX_NAMESPACE WaitForPCGData

#pragma region Config Struct Implementations

void PCGExWaitForPCGData::FFilterConfig::InitFrom(const UPCGExWaitForPCGDataSettings* Settings)
{
	bMustMatchTemplate = Settings->bMustMatchTemplate;
	MustHaveTag = Settings->MustHaveTag;
	bDoMatchGenerationTrigger = Settings->bDoMatchGenerationTrigger;
	MatchGenerationTrigger = Settings->MatchGenerationTrigger;
	bInvertGenerationTrigger = Settings->bInvertGenerationTrigger;
}

bool PCGExWaitForPCGData::FFilterConfig::PassesFilter(const UPCGComponent* Candidate, const UPCGGraph* TemplateGraph, const UPCGComponent* Self) const
{
	const UPCGGraph* CandidateGraph = Candidate->GetGraph();

	// Basic validity checks
	if (!CandidateGraph || !Candidate->bActivated) { return false; }
	if (Self && Candidate == Self) { return false; }

	// Template matching
	if (bMustMatchTemplate && CandidateGraph != TemplateGraph) { return false; }

	// Tag matching
	if (!MustHaveTag.IsNone() && !Candidate->ComponentHasTag(MustHaveTag)) { return false; }

	// Generation trigger matching
	if (bDoMatchGenerationTrigger)
	{
		const bool bMatches = Candidate->GenerationTrigger == MatchGenerationTrigger;
		if (bInvertGenerationTrigger ? bMatches : !bMatches) { return false; }
	}

	return true;
}

void PCGExWaitForPCGData::FTimeoutConfig::InitFrom(const UPCGExWaitForPCGDataSettings* Settings)
{
	bWaitForMissingActors = Settings->bWaitForMissingActors;
	WaitForActorTimeout = Settings->WaitForActorTimeout;
	bWaitForMissingComponents = Settings->bWaitForMissingComponents;
	WaitForComponentTimeout = Settings->WaitForComponentTimeout;
}

void PCGExWaitForPCGData::FGenerationConfig::InitFrom(const UPCGExWaitForPCGDataSettings* Settings)
{
	GenerateOnLoadAction = Settings->GenerateOnLoadAction;
	GenerateOnDemandAction = Settings->GenerateOnDemandAction;
	GenerateAtRuntimeAction = Settings->GenerateAtRuntime;
}

void PCGExWaitForPCGData::FOutputConfig::InitFrom(const UPCGExWaitForPCGDataSettings* Settings)
{
	bIgnoreRequiredPin = Settings->bIgnoreRequiredPin;
	bDedupeData = Settings->bDedupeData;
	bCarryOverTargetTags = Settings->bCarryOverTargetTags;
	bOutputRoaming = Settings->bOutputRoaming;
	RoamingPin = Settings->RoamingPin;
}

void PCGExWaitForPCGData::FWarningConfig::InitFrom(const UPCGExWaitForPCGDataSettings* Settings)
{
	bQuietActorNotFoundWarning = Settings->bQuietActorNotFoundWarning;
	bQuietComponentNotFoundWarning = Settings->bQuietComponentNotFoundWarning;
	bQuietTimeoutError = Settings->bQuietTimeoutError;
}

#pragma endregion

#pragma region Settings

UPCGExWaitForPCGDataSettings::UPCGExWaitForPCGDataSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void UPCGExWaitForPCGDataSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPCGExWaitForPCGDataSettings, TemplateGraph))
	{
		EDITOR_RefreshPins();
	}
}
#endif

bool UPCGExWaitForPCGDataSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	return true;
}

TArray<FPCGPinProperties> UPCGExWaitForPCGDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	FPCGPinProperties& DependencyPin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any, /*bInAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true);
	DependencyPin.Usage = EPCGPinUsage::DependencyOnly;

	if (bOutputRoaming) { PCGEX_PIN_ANY(RoamingPin, "Roaming data that isn't part of the template output but still exists.", Normal) }

	PinProperties.Append(CachedPins);

	return PinProperties;
}

#if WITH_EDITOR
void UPCGExWaitForPCGDataSettings::EDITOR_RefreshPins()
{
	Modify(true);

	GetTargetGraphPins(CachedPins); // Force-refresh cached pins

	FPropertyChangedEvent EmptyEvent(nullptr);
	PostEditChangeProperty(EmptyEvent);
	MarkPackageDirty();
}
#endif

void FPCGExWaitForPCGDataContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(WaitForPCGData)

	GraphInstancePaths.Reserve(MainPoints->Pairs.Num());

	if (Settings->TemplateInput == EPCGExDataInputValueType::Attribute)
	{
		for (const TSharedPtr<PCGExData::FPointIO>& IO : MainPoints->Pairs)
		{
			FSoftObjectPath Path = FSoftObjectPath();
			if (PCGExData::Helpers::TryReadDataValue(this, IO->GetIn(), Settings->TemplateGraphAttributeName, Path)) { AddAssetDependency(Path); }
			GraphInstancePaths.Add(Path);
		}
	}
	else
	{
		FSoftObjectPath Path = Settings->TemplateGraph.ToSoftObjectPath();
		AddAssetDependency(Path);
		for (const TSharedPtr<PCGExData::FPointIO>& IO : MainPoints->Pairs) { GraphInstancePaths.Add(Path); }
	}

	FPCGExPointsProcessorContext::RegisterAssetDependencies();
}

PCGEX_INITIALIZE_ELEMENT(WaitForPCGData)
PCGEX_ELEMENT_BATCH_POINT_IMPL(WaitForPCGData)

void UPCGExWaitForPCGDataSettings::GetTargetGraphPins(TArray<FPCGPinProperties>& OutPins) const
{
	OutPins.Empty();
	PCGExHelpers::LoadBlocking_AnyThreadTpl(TemplateGraph);
	if (const UPCGGraph* PinPropertiesSource = TemplateGraph.Get())
	{
		TArray<FPCGPinProperties> FoundPins = PinPropertiesSource->GetOutputNode()->OutputPinProperties();
		OutPins.Reserve(FoundPins.Num());
		for (FPCGPinProperties Pin : FoundPins)
		{
			Pin.bInvisiblePin = false;
			OutPins.Add(Pin);
		}
	}
}

FName UPCGExWaitForPCGDataSettings::GetMainInputPin() const
{
	return PCGExCommon::Labels::SourceTargetsLabel;
}

#pragma endregion

#pragma region Element

bool FPCGExWaitForPCGDataElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(WaitForPCGData)

	PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->ActorReferenceAttribute)
	if (Settings->TemplateInput == EPCGExDataInputValueType::Attribute) { PCGEX_VALIDATE_NAME(Settings->TemplateGraphAttributeName) }

	for (FPCGPinProperties Pin : Settings->CachedPins)
	{
		Context->AllLabels.Add(Pin.Label);

		if (Pin.IsRequiredPin())
		{
			Context->RequiredPinProperties.Add(Pin);
			Context->RequiredLabels.Add(Pin.Label);
		}
	}

	// Initialize config structs from settings
	Context->FilterConfig.InitFrom(Settings);
	Context->TimeoutConfig.InitFrom(Settings);
	Context->GenerationConfig.InitFrom(Settings);
	Context->OutputConfig.InitFrom(Settings);
	Context->WarningConfig.InitFrom(Settings);

	return true;
}

bool FPCGExWaitForPCGDataElement::PostBoot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::PostBoot(InContext)) { return false; }

	FPCGExWaitForPCGDataContext* Context = static_cast<FPCGExWaitForPCGDataContext*>(InContext);
	for (const FSoftObjectPath& Path : Context->GraphInstancePaths)
	{
		TSoftObjectPtr<UPCGGraph> Graph = TSoftObjectPtr<UPCGGraph>(Path);
		if (!Graph.Get())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some graph could not be loaded."));
			return false;
		}
		Context->GraphInstances.Add(Graph.Get());
	}

	return true;
}

bool FPCGExWaitForPCGDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWaitForPCGDataElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WaitForPCGData)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	return Context->TryComplete();
}

#pragma endregion

#pragma region Component Discovery

namespace PCGExWaitForPCGData
{
	FComponentDiscovery::FComponentDiscovery(
		FPCGExWaitForPCGDataContext* InContext,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		UPCGGraph* InTemplateGraph,
		const FFilterConfig& InFilterConfig,
		const FTimeoutConfig& InTimeoutConfig,
		const FWarningConfig& InWarningConfig)
		: Context(InContext)
		  , TaskManagerWeak(InTaskManager)
		  , TemplateGraph(InTemplateGraph)
		  , FilterConfig(InFilterConfig)
		  , TimeoutConfig(InTimeoutConfig)
		  , WarningConfig(InWarningConfig)
	{
	}

	FComponentDiscovery::~FComponentDiscovery()
	{
		Stop();
	}

	bool FComponentDiscovery::Start(const TSet<FSoftObjectPath>& InActorReferences)
	{
		TSharedPtr<PCGExMT::FTaskManager> TaskManager = TaskManagerWeak.Pin();
		if (!TaskManager) { return false; }

		UniqueActorReferences = InActorReferences;
		QueuedActors.Reserve(UniqueActorReferences.Num());

		InspectionTracker = MakeShared<FPCGExIntTracker>([WeakThis = TWeakPtr<FComponentDiscovery>(SharedThis(this))]()
		{
			if (TSharedPtr<FComponentDiscovery> This = WeakThis.Pin())
			{
				This->OnInspectionCompleteInternal();
			}
		});

		if (TimeoutConfig.bWaitForMissingActors)
		{
			StartTime = Context->GetWorld()->GetTimeSeconds();
			SearchActorsToken = TaskManager->TryCreateToken(FName("SearchActors"));
			if (!SearchActorsToken.IsValid()) { return false; }
			GatherActors();
		}
		else
		{
			bool bHasUnresolvedReferences = false;

			for (const FSoftObjectPath& ActorRef : UniqueActorReferences)
			{
				if (AActor* Actor = Cast<AActor>(ActorRef.ResolveObject())) { QueuedActors.Add(Actor); }
				else { bHasUnresolvedReferences = true; }
			}

			if (QueuedActors.IsEmpty() && !WarningConfig.bQuietActorNotFoundWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Could not resolve any actor references."));
				return false;
			}

			if (bHasUnresolvedReferences && !WarningConfig.bQuietActorNotFoundWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Some actor references could not be resolved."));
			}

			// Start component search
			SearchComponentsToken = TaskManager->TryCreateToken(FName("SearchComponents"));
			if (!SearchComponentsToken.IsValid()) { return false; }

			StartTime = Context->GetWorld()->GetTimeSeconds();

			PCGEX_SUBSYSTEM
			PCGExSubsystem->RegisterBeginTickAction([WeakThis = TWeakPtr<FComponentDiscovery>(SharedThis(this))]()
			{
				if (TSharedPtr<FComponentDiscovery> This = WeakThis.Pin())
				{
					This->GatherComponents();
				}
			});
		}

		return true;
	}

	void FComponentDiscovery::Stop()
	{
		PCGEX_ASYNC_RELEASE_TOKEN(SearchActorsToken)
		PCGEX_ASYNC_RELEASE_TOKEN(SearchComponentsToken)
	}

	void FComponentDiscovery::GatherActors()
	{
		if (!SearchActorsToken.IsValid()) { return; }

		TSharedPtr<PCGExMT::FTaskManager> TaskManager = TaskManagerWeak.Pin();
		if (!TaskManager || !TaskManager->IsAvailable())
		{
			PCGEX_ASYNC_RELEASE_TOKEN(SearchActorsToken)
			return;
		}

		bool bHasUnresolvedReferences = false;

		QueuedActors.Reset(UniqueActorReferences.Num());
		for (const FSoftObjectPath& ActorRef : UniqueActorReferences)
		{
			if (AActor* Actor = Cast<AActor>(ActorRef.ResolveObject())) { QueuedActors.Add(Actor); }
			else { bHasUnresolvedReferences = true; }
		}

		if (bHasUnresolvedReferences)
		{
			if (Context->GetWorld()->GetTimeSeconds() - StartTime < TimeoutConfig.WaitForActorTimeout)
			{
				PCGEX_SUBSYSTEM
				PCGExSubsystem->RegisterBeginTickAction([WeakThis = TWeakPtr<FComponentDiscovery>(SharedThis(this))]()
				{
					if (TSharedPtr<FComponentDiscovery> This = WeakThis.Pin())
					{
						This->GatherActors();
					}
				});
				return;
			}

			// Timeout - log errors
			if (!WarningConfig.bQuietTimeoutError)
			{
				for (const FSoftObjectPath& ActorRef : UniqueActorReferences)
				{
					if (Cast<AActor>(ActorRef.ResolveObject())) { continue; }
					FString Rel = TEXT("TIMEOUT : ") + ActorRef.ToString() + TEXT(" not found.");
					PCGE_LOG_C(Error, GraphAndLog, Context, FText::FromString(Rel));
				}
			}

			PCGEX_ASYNC_RELEASE_TOKEN(SearchActorsToken)
			if (OnDiscoveryComplete) { OnDiscoveryComplete(); }
			return;
		}

		// All actors found - start component search
		PCGEX_ASYNC_RELEASE_TOKEN(SearchActorsToken)

		SearchComponentsToken = TaskManager->TryCreateToken(FName("SearchComponents"));
		if (!SearchComponentsToken.IsValid())
		{
			if (OnDiscoveryComplete) { OnDiscoveryComplete(); }
			return;
		}

		StartTime = Context->GetWorld()->GetTimeSeconds();

		PCGEX_SUBSYSTEM
		PCGExSubsystem->RegisterBeginTickAction([WeakThis = TWeakPtr<FComponentDiscovery>(SharedThis(this))]()
		{
			if (TSharedPtr<FComponentDiscovery> This = WeakThis.Pin())
			{
				This->GatherComponents();
			}
		});
	}

	void FComponentDiscovery::GatherComponents()
	{
		if (!SearchComponentsToken.IsValid()) { return; }

		TSharedPtr<PCGExMT::FTaskManager> TaskManager = TaskManagerWeak.Pin();
		if (!TaskManager || !TaskManager->IsAvailable())
		{
			Stop();
			if (OnDiscoveryComplete) { OnDiscoveryComplete(); }
			return;
		}

		PerActorGatheredComponents.Reset();
		PerActorGatheredComponents.SetNum(QueuedActors.Num());

		for (int i = 0; i < QueuedActors.Num(); i++)
		{
			QueuedActors[i]->GetComponents(UPCGComponent::StaticClass(), PerActorGatheredComponents[i]);
		}

		InspectGatheredComponents();
	}

	void FComponentDiscovery::InspectGatheredComponents()
	{
		if (!SearchComponentsToken.IsValid()) { return; }

		TSharedPtr<PCGExMT::FTaskManager> TaskManager = TaskManagerWeak.Pin();
		if (!TaskManager || !TaskManager->IsAvailable())
		{
			Stop();
			if (OnDiscoveryComplete) { OnDiscoveryComplete(); }
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWaitForPCGData::InspectComponents);

		InspectionTracker->Reset(QueuedActors.Num());

		TWeakPtr<FComponentDiscovery> WeakDiscovery = SharedThis(this);
		for (int i = 0; i < QueuedActors.Num(); i++)
		{
			UE::Tasks::Launch(TEXT("ComponentInspection"), [WeakDiscovery, Index = i]()
			{
				if (TSharedPtr<FComponentDiscovery> Discovery = WeakDiscovery.Pin())
				{
					Discovery->Inspect(Index);
				}
			}, UE::Tasks::ETaskPriority::BackgroundLow);
		}
	}

	void FComponentDiscovery::Inspect(int32 Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWaitForPCGData::Inspect);

		ON_SCOPE_EXIT { InspectionTracker->IncrementCompleted(); };

		UPCGComponent* Self = Context->GetMutableComponent();

		// Move the array since we don't need the original after inspection
		TArray<UPCGComponent*> FoundComponents = MoveTemp(PerActorGatheredComponents[Index]);

		FoundComponents.RemoveAll([this, Self](const UPCGComponent* Candidate) -> bool
		{
			// Remove if doesn't pass filter
			if (!IsValidCandidate(Candidate)) { return true; }

			// Remove if candidate is self
			if (Self && Candidate == Self) { return true; }

			// Check required pins if not matching template
			if (!FilterConfig.bMustMatchTemplate)
			{
				if (!HasRequiredPins(Candidate->GetGraph())) { return true; }
			}

			return false;
		});

		if (TimeoutConfig.bWaitForMissingComponents && FoundComponents.IsEmpty())
		{
			// Wait until next run - don't mark actor as processed
			return;
		}

		// Mark actor as processed (set to nullptr so it's removed from queue)
		QueuedActors[Index] = nullptr;

		// Notify about found components
		for (UPCGComponent* PCGComponent : FoundComponents)
		{
			Context->EDITOR_TrackPath(PCGComponent);
			if (OnComponentFound)
			{
				OnComponentFound(PCGComponent);
			}
		}
	}

	void FComponentDiscovery::OnInspectionCompleteInternal()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWaitForPCGData::OnInspectionComplete);

		// Compact the queued actors array - remove processed (nullptr) entries
		QueuedActors.RemoveAll([](const AActor* Actor) { return Actor == nullptr; });

		// If some actors still have no valid components, retry or timeout
		if (!QueuedActors.IsEmpty())
		{
			if (Context->GetWorld()->GetTimeSeconds() - StartTime < TimeoutConfig.WaitForComponentTimeout)
			{
				PCGEX_SUBSYSTEM
				PCGExSubsystem->RegisterBeginTickAction([WeakThis = TWeakPtr<FComponentDiscovery>(SharedThis(this))]()
				{
					if (TSharedPtr<FComponentDiscovery> This = WeakThis.Pin())
					{
						This->GatherComponents();
					}
				});
				return;
			}

			// Timeout
			if (!WarningConfig.bQuietTimeoutError)
			{
				for (const AActor* Actor : QueuedActors)
				{
					FString Rel = TEXT("TIMEOUT : ") + Actor->GetName() + TEXT(" does not have ") + TemplateGraph.GetName();
					PCGE_LOG_C(Error, GraphAndLog, Context, FText::FromString(Rel));
				}
			}
		}

		Stop();

		if (OnDiscoveryComplete)
		{
			OnDiscoveryComplete();
		}
	}

	bool FComponentDiscovery::IsValidCandidate(const UPCGComponent* Candidate) const
	{
		return FilterConfig.PassesFilter(Candidate, TemplateGraph.Get(), Context->GetMutableComponent());
	}

	bool FComponentDiscovery::HasRequiredPins(const UPCGGraph* CandidateGraph) const
	{
		if (!CandidateGraph) { return false; }

		TArray<FPCGPinProperties> OutPins = CandidateGraph->GetOutputNode()->OutputPinProperties();

		for (const FName& Label : Context->RequiredLabels)
		{
			bool bFound = false;
			for (const FPCGPinProperties& Pin : OutPins)
			{
				if (Pin.Label == Label)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound) { return false; }
		}

		return true;
	}
}

#pragma endregion

#pragma region Data Stager

namespace PCGExWaitForPCGData
{
	FDataStager::FDataStager(
		FPCGExWaitForPCGDataContext* InContext,
		const TSharedRef<PCGExData::FFacade>& InPointDataFacade,
		const FOutputConfig& InOutputConfig,
		const FPCGExAttributeToTagDetails& InTagDetails)
		: Context(InContext)
		  , PointDataFacade(InPointDataFacade)
		  , OutputConfig(InOutputConfig)
		  , TagDetails(InTagDetails)
	{
	}

	void FDataStager::StageComponentData(UPCGComponent* InComponent, const TArray<int32>& MatchingPointIndices)
	{
		const FPCGDataCollection& GraphOutput = InComponent->GetGeneratedGraphOutput();

		if (GraphOutput.TaggedData.IsEmpty()) { return; }

		// Ensure we have all required pins first
		if (!OutputConfig.bIgnoreRequiredPin)
		{
			for (const FName Required : Context->RequiredLabels)
			{
				if (GraphOutput.GetInputsByPin(Required).IsEmpty())
				{
					return;
				}
			}
		}

		Context->IncreaseStagedOutputReserve(
			OutputConfig.bDedupeData
				? GraphOutput.TaggedData.Num()
				: GraphOutput.TaggedData.Num() * MatchingPointIndices.Num());

		if (OutputConfig.bDedupeData)
		{
			// Dedupe mode: use first point's tags only
			TSet<FString> PointsTags;
			TagDetails.Tag(PointDataFacade->GetInPoint(MatchingPointIndices[0]), PointsTags);
			if (OutputConfig.bCarryOverTargetTags) { PointDataFacade->Source->Tags->DumpTo(PointsTags); }

			for (const FPCGTaggedData& TaggedData : GraphOutput.TaggedData)
			{
				StageTaggedData(TaggedData, PointsTags);
			}
		}
		else
		{
			// Non-dedupe mode: output for each matching point
			for (const int32 PtIndex : MatchingPointIndices)
			{
				TSet<FString> PointsTags;
				TagDetails.Tag(PointDataFacade->GetInPoint(PtIndex), PointsTags);
				if (OutputConfig.bCarryOverTargetTags) { PointDataFacade->Source->Tags->DumpTo(PointsTags); }

				for (const FPCGTaggedData& TaggedData : GraphOutput.TaggedData)
				{
					StageTaggedData(TaggedData, PointsTags);
				}
			}
		}
	}

	void FDataStager::StageTaggedData(const FPCGTaggedData& TaggedData, const TSet<FString>& PointsTags)
	{
		TSet<FString> DataTags = TaggedData.Tags;
		DataTags.Append(PointsTags);

		if (!Context->AllLabels.Contains(TaggedData.Pin))
		{
			if (OutputConfig.bOutputRoaming)
			{
				Context->StageOutput(const_cast<UPCGData*>(TaggedData.Data.Get()), OutputConfig.RoamingPin, PCGExData::EStaging::None, DataTags);
			}
			return;
		}

		Context->StageOutput(const_cast<UPCGData*>(TaggedData.Data.Get()), TaggedData.Pin, PCGExData::EStaging::None, DataTags);
	}
}

#pragma endregion

#pragma region Processor

namespace PCGExWaitForPCGData
{
	class FStageComponentDataTask final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		explicit FStageComponentDataTask(const int32 InTaskIndex, const TWeakPtr<FProcessor>& InProcessor, UPCGComponent* InComponent)
			: FPCGExIndexedTask(InTaskIndex)
			  , Processor(InProcessor)
			  , Component(InComponent)
		{
		}

		const TWeakPtr<FProcessor> Processor;
		TWeakObjectPtr<UPCGComponent> Component;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			if (const TSharedPtr<FProcessor> P = Processor.Pin())
			{
				if (UPCGComponent* Comp = Component.Get())
				{
					P->DoStageComponentData(Comp);
				}
			}
		}
	};

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWaitForPCGData::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		TemplateGraph = Context->GraphInstances[PointDataFacade->Source->IOIndex];

		// Setup attribute to tags
		TargetAttributesToDataTags = Settings->TargetAttributesToDataTags;
		if (Settings->bDedupeData) { TargetAttributesToDataTags.bAddIndexTag = false; }
		if (!TargetAttributesToDataTags.Init(Context, PointDataFacade)) { return false; }

		// Gather actor references from points
		PCGEX_MAKE_SHARED(ActorReferences, PCGExData::TAttributeBroadcaster<FSoftObjectPath>)

		if (!ActorReferences->Prepare(Settings->ActorReferenceAttribute, PointDataFacade->Source))
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some inputs don't have the specified Actor Reference attribute."));
			return false;
		}

		ActorReferences->Grab();
		for (int i = 0; i < ActorReferences->Values.Num(); i++)
		{
			const FSoftObjectPath& ActorRef = ActorReferences->Values[i];
			if (!ActorRef.IsValid()) { continue; }

			bool bAlreadySet = false;
			UniqueActorReferences.Add(ActorRef, &bAlreadySet);

			if (bAlreadySet)
			{
				PerActorPoints[ActorRef]->Add(i);
			}
			else
			{
				PCGEX_MAKE_SHARED(PointList, TArray<int32>)
				PointList->Add(i);
				PerActorPoints.Add(ActorRef, PointList);
			}
		}

		// Pre-reserve component tracking collections
		IndexedComponents.Reserve(UniqueActorReferences.Num());
		ComponentToIndex.Reserve(UniqueActorReferences.Num());

		// Create sub-systems
		Discovery = MakeShared<FComponentDiscovery>(
			Context,
			TaskManager,
			TemplateGraph.Get(),
			Context->FilterConfig,
			Context->TimeoutConfig,
			Context->WarningConfig);

		Watcher = MakeShared<FGenerationWatcher>(
			TaskManager,
			Context->GenerationConfig);
		Watcher->Initialize();

		Stager = MakeShared<FDataStager>(
			Context,
			PointDataFacade,
			Context->OutputConfig,
			TargetAttributesToDataTags);

		// Wire up callbacks
		Discovery->SetOnComponentFound([WeakThis = TWeakPtr<FProcessor>(SharedThis(this))](UPCGComponent* Comp)
		{
			if (TSharedPtr<FProcessor> This = WeakThis.Pin())
			{
				This->OnComponentFound(Comp);
			}
		});

		Watcher->SetOnGenerationComplete([WeakThis = TWeakPtr<FProcessor>(SharedThis(this))](UPCGComponent* Comp, bool bSuccess)
		{
			if (TSharedPtr<FProcessor> This = WeakThis.Pin())
			{
				This->OnGenerationComplete(Comp, bSuccess);
			}
		});

		// Start discovery
		if (!Discovery->Start(UniqueActorReferences))
		{
			if (!Context->WarningConfig.bQuietActorNotFoundWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Failed to start component discovery."));
			}
			return false;
		}

		return true;
	}

	void FProcessor::OnComponentFound(UPCGComponent* InComponent)
	{
		// Track component for later staging
		{
			FWriteScopeLock WriteLock(ComponentLock);
			if (ComponentToIndex.Find(InComponent)) { return; } // Already tracked

			int32 Index = IndexedComponents.Add(InComponent);
			ComponentToIndex.Emplace(InComponent, Index);
		}

		// Hand off to watcher
		Watcher->Watch(InComponent);
	}

	void FProcessor::OnGenerationComplete(UPCGComponent* InComponent, bool bSuccess)
	{
		if (bSuccess)
		{
			ScheduleDataStaging(InComponent);
		}
	}

	void FProcessor::ScheduleDataStaging(UPCGComponent* InComponent)
	{
		int32 Index = 0;
		{
			FReadScopeLock ReadLock(ComponentLock);
			if (int32* FoundIndex = ComponentToIndex.Find(InComponent))
			{
				Index = *FoundIndex;
			}
			else
			{
				return; // Unknown component
			}
		}

		PCGEX_MAKE_SHARED(Task, FStageComponentDataTask, Index, SharedThis(this), InComponent)
		TaskManager->Launch(Task);
	}

	void FProcessor::DoStageComponentData(UPCGComponent* InComponent)
	{
		const TSharedPtr<TArray<int32>>* MatchingPointsPtr = PerActorPoints.Find(InComponent->GetOwner()->GetPathName());
		if (!MatchingPointsPtr) { return; }

		Stager->StageComponentData(InComponent, *MatchingPointsPtr->Get());
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
