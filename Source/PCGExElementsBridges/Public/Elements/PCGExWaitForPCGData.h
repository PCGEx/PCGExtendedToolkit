// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGComponent.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExPCGGenerationWatcher.h"


#include "PCGExWaitForPCGData.generated.h"

class FPCGExIntTracker;

namespace PCGExMT
{
	class FAsyncToken;
}

// Forward declaration for config structs
class UPCGExWaitForPCGDataSettings;

//
// Internal config structs - Used to bundle settings for cleaner internal APIs.
// These are populated from UPCGExWaitForPCGDataSettings during Boot/Process
// and can be promoted to USTRUCT UPROPERTYs in a future major version.
//

namespace PCGExWaitForPCGData
{
	/** Filter configuration for component discovery */
	struct FFilterConfig
	{
		bool bMustMatchTemplate = true;
		FName MustHaveTag = NAME_None;
		bool bDoMatchGenerationTrigger = false;
		EPCGComponentGenerationTrigger MatchGenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
		bool bInvertGenerationTrigger = false;

		void InitFrom(const UPCGExWaitForPCGDataSettings* Settings);
		bool PassesFilter(const UPCGComponent* Candidate, const UPCGGraph* TemplateGraph, const UPCGComponent* Self) const;
	};

	/** Timeout configuration for async waiting */
	struct FTimeoutConfig
	{
		bool bWaitForMissingActors = true;
		double WaitForActorTimeout = 1.0;
		bool bWaitForMissingComponents = false;
		double WaitForComponentTimeout = 1.0;

		void InitFrom(const UPCGExWaitForPCGDataSettings* Settings);
	};

	/** Generation trigger action configuration — alias to shared type in PCGExPCGInterop */
	struct FGenerationConfig : public PCGExPCGInterop::FGenerationConfig
	{
		void InitFrom(const UPCGExWaitForPCGDataSettings* Settings);
	};

	/** Output configuration */
	struct FOutputConfig
	{
		bool bIgnoreRequiredPin = false;
		bool bDedupeData = true;
		bool bCarryOverTargetTags = true;
		bool bOutputRoaming = true;
		FName RoamingPin = FName("Roaming Data");

		void InitFrom(const UPCGExWaitForPCGDataSettings* Settings);
	};

	/** Warning/error suppression configuration */
	struct FWarningConfig
	{
		bool bQuietActorNotFoundWarning = false;
		bool bQuietComponentNotFoundWarning = false;
		bool bQuietTimeoutError = false;

		void InitFrom(const UPCGExWaitForPCGDataSettings* Settings);
	};
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling")
class UPCGExWaitForPCGDataSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExWaitForPCGDataSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UObject interface
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(WaitForPCGData, "Wait for PCG Data", "Wait for PCG Components Generated output.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::ControlFlow; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Action); }
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings


	//~Begin UPCGExPointsProcessorSettings

public:
	void GetTargetGraphPins(TArray<FPCGPinProperties>& OutPins) const;
	virtual FName GetMainInputPin() const override;
	//~End UPCGExPointsProcessorSettings

	/** Actor reference that we will be waiting for PCG Components with the target graph. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName ActorReferenceAttribute = FName(TEXT("ActorReference"));

	/** Actor reference that we will be waiting for PCG Components with the target graph. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExDataInputValueType TemplateInput = EPCGExDataInputValueType::Constant;

	/** Graph instance to look for. Will wait until a PCGComponent is found with that instance set, and its output generated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="TemplateInput == EPCGExDataInputValueType::Constant"))
	TSoftObjectPtr<UPCGGraph> TemplateGraph;

	/** Graph instance to look for. Will wait until a PCGComponent is found with that instance set, and its output generated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="TemplateInput == EPCGExDataInputValueType::Attribute"))
	FName TemplateGraphAttributeName = FName("@Data.TemplateGraph");

	/** If enabled, will skip components which graph instances is not the same as the specified template. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable))
	bool bMustMatchTemplate = true;

	/** If not None, will only consider components with the specified tag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable))
	FName MustHaveTag = NAME_None;

	/** Filter components by their generation trigger type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bDoMatchGenerationTrigger = false;

	/** If enabled, only process component with the specified generation trigger. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable, EditCondition="bDoMatchGenerationTrigger"))
	EPCGComponentGenerationTrigger MatchGenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;

	/** If enabled, only process component that do not match the specified generation trigger */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Filtering", meta = (PCG_Overridable, DisplayName=" └─ Invert", EditCondition="bDoMatchGenerationTrigger", EditConditionHides, HideEditConditionToggle))
	bool bInvertGenerationTrigger = false;

	/** If enabled, will wait for actor references to exist. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable))
	bool bWaitForMissingActors = true;

	/** Time after which the search is considered a fail. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable, DisplayName=" └─ Timeout", EditCondition="bWaitForMissingActors", EditConditionHides, ClampMin=0.001, ClampMax=30))
	double WaitForActorTimeout = 1;

	/** If enabled, will wait for at least a single PCG component to be found that uses the target Graph. Use carefully, and only if you know for sure it will be found at some point! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable))
	bool bWaitForMissingComponents = false;

	/** Time after which the search is considered a fail. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable, DisplayName=" └─ Timeout", EditCondition="bWaitForMissingActors", EditConditionHides, ClampMin=0.001, ClampMax=30))
	double WaitForComponentTimeout = 1;

	/** How to deal with found components that have the trigger condition 'GenerateOnLoad'*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable, DisplayName="Grab GenerateOnLoad"))
	EPCGExGenerationTriggerAction GenerateOnLoadAction = EPCGExGenerationTriggerAction::Generate;

	/** How to deal with found components that have the trigger condition 'GenerateOnDemand'*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable, DisplayName="Grab GenerateOnDemand"))
	EPCGExGenerationTriggerAction GenerateOnDemandAction = EPCGExGenerationTriggerAction::Generate;

	/** How to deal with found components that have the trigger condition 'GenerateAtRuntime'*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Gen & Wait Settings", meta = (PCG_Overridable, DisplayName="Grab GenerateAtRuntime"))
	EPCGExRuntimeGenerationTriggerAction GenerateAtRuntime = EPCGExRuntimeGenerationTriggerAction::AsIs;

	/** If enabled, available data will be output even if some required pins have no data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable))
	bool bIgnoreRequiredPin = false;

	/** If enabled, only output component data once per unique actor. Otherwise, output data as many time as found. Note that when enabled, TargetIndexToTag will be disabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	bool bDedupeData = true;

	/** If enabled, target collections' tags will be added to the output data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	bool bCarryOverTargetTags = true;

	/** Lets you tag output data with attribute values from target points input */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FPCGExAttributeToTagDetails TargetAttributesToDataTags;

	/** Output data that doesn't match any template pin to a separate roaming pin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(InlineEditConditionToggle))
	bool bOutputRoaming = true;

	/** If enabled, adds an extra pin that contains all the data that isn't part of the template. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(EditCondition="bOutputRoaming"))
	FName RoamingPin = FName("Roaming Data");

	/** Suppress warnings when referenced actors are not found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietActorNotFoundWarning = false;

	/** Suppress warnings when no matching PCG components are found on actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietComponentNotFoundWarning = false;

	/** Suppress errors when waiting for components times out. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTimeoutError = false;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = Utils, meta=(DisplayName="Refresh Pins", ShortToolTip="Refreshes the cached pins"))
	void EDITOR_RefreshPins();
#endif

	UPROPERTY()
	TArray<FPCGPinProperties> CachedPins;
};

struct FPCGExWaitForPCGDataContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExWaitForPCGDataElement;

	virtual void RegisterAssetDependencies() override;

	TArray<FPCGPinProperties> RequiredPinProperties;
	TSet<FName> AllLabels;
	TSet<FName> RequiredLabels;

	TArray<FSoftObjectPath> GraphInstancePaths;
	TArray<UPCGGraph*> GraphInstances;

	// Internal config structs - populated during Boot from Settings
	PCGExWaitForPCGData::FFilterConfig FilterConfig;
	PCGExWaitForPCGData::FTimeoutConfig TimeoutConfig;
	PCGExWaitForPCGData::FGenerationConfig GenerationConfig;
	PCGExWaitForPCGData::FOutputConfig OutputConfig;
	PCGExWaitForPCGData::FWarningConfig WarningConfig;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExWaitForPCGDataElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WaitForPCGData)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWaitForPCGData
{
	//
	// Component Discovery - Handles finding and filtering PCG components on target actors
	//
	// State Machine:
	// [Start] -> GatherActors -> (wait for actors if needed) -> GatherComponents -> InspectComponents -> [Complete]
	//                ^                                                                    |
	//                +-----------------(retry if waiting for components)------------------+
	//
	class FComponentDiscovery final : public TSharedFromThis<FComponentDiscovery>
	{
	public:
		using FOnDiscoveryComplete = TFunction<void(UPCGComponent*)>;

		FComponentDiscovery(
			FPCGExWaitForPCGDataContext* InContext,
			const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
			UPCGGraph* InTemplateGraph,
			const FFilterConfig& InFilterConfig,
			const FTimeoutConfig& InTimeoutConfig,
			const FWarningConfig& InWarningConfig);

		~FComponentDiscovery();

		void SetOnComponentFound(FOnDiscoveryComplete&& InCallback) { OnComponentFound = MoveTemp(InCallback); }
		void SetOnDiscoveryComplete(TFunction<void()>&& InCallback) { OnDiscoveryComplete = MoveTemp(InCallback); }

		bool Start(const TSet<FSoftObjectPath>& InActorReferences);
		void Stop();

	private:
		void GatherActors();
		void GatherComponents();
		void InspectGatheredComponents();
		void Inspect(int32 Index);
		void OnInspectionCompleteInternal();

		bool IsValidCandidate(const UPCGComponent* Candidate) const;
		bool HasRequiredPins(const UPCGGraph* CandidateGraph) const;

		FPCGExWaitForPCGDataContext* Context = nullptr;
		TWeakPtr<PCGExMT::FTaskManager> TaskManagerWeak;
		TObjectPtr<UPCGGraph> TemplateGraph;

		FFilterConfig FilterConfig;
		FTimeoutConfig TimeoutConfig;
		FWarningConfig WarningConfig;

		TWeakPtr<PCGExMT::FAsyncToken> SearchActorsToken;
		TWeakPtr<PCGExMT::FAsyncToken> SearchComponentsToken;
		TSharedPtr<FPCGExIntTracker> InspectionTracker;

		double StartTime = 0;
		TSet<FSoftObjectPath> UniqueActorReferences;
		TArray<AActor*> QueuedActors;
		TArray<TArray<UPCGComponent*>> PerActorGatheredComponents;

		FOnDiscoveryComplete OnComponentFound;
		TFunction<void()> OnDiscoveryComplete;
	};

	//
	// Generation Watcher — alias to shared type in PCGExPCGInterop
	//
	using FGenerationWatcher = PCGExPCGInterop::FGenerationWatcher;

	//
	// Data Stager - Handles extracting and staging component output data
	//
	class FDataStager final
	{
	public:
		FDataStager(
			FPCGExWaitForPCGDataContext* InContext,
			const TSharedRef<PCGExData::FFacade>& InPointDataFacade,
			const FOutputConfig& InOutputConfig,
			const FPCGExAttributeToTagDetails& InTagDetails);

		void StageComponentData(
			UPCGComponent* InComponent,
			const TArray<int32>& MatchingPointIndices);

	private:
		void StageTaggedData(
			const FPCGTaggedData& TaggedData,
			const TSet<FString>& PointsTags);

		FPCGExWaitForPCGDataContext* Context = nullptr;
		TSharedRef<PCGExData::FFacade> PointDataFacade;
		FOutputConfig OutputConfig;
		FPCGExAttributeToTagDetails TagDetails;
	};

	//
	// Processor - Orchestrates component discovery, generation watching, and data staging
	//
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExWaitForPCGDataContext, UPCGExWaitForPCGDataSettings>
	{
		friend class FStageComponentDataTask;

		TObjectPtr<UPCGGraph> TemplateGraph;

		// Sub-systems
		TSharedPtr<FComponentDiscovery> Discovery;
		TSharedPtr<FGenerationWatcher> Watcher;
		TSharedPtr<FDataStager> Stager;

		// Actor reference to point index mapping
		TMap<FSoftObjectPath, TSharedPtr<TArray<int32>>> PerActorPoints;
		TSet<FSoftObjectPath> UniqueActorReferences;

		// Component tracking for data staging
		FRWLock ComponentLock;
		TMap<UPCGComponent*, int32> ComponentToIndex;
		TArray<UPCGComponent*> IndexedComponents;

		FPCGExAttributeToTagDetails TargetAttributesToDataTags;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

	private:
		void OnComponentFound(UPCGComponent* InComponent);
		void OnGenerationComplete(UPCGComponent* InComponent, bool bSuccess);
		void ScheduleDataStaging(UPCGComponent* InComponent);
		void DoStageComponentData(UPCGComponent* InComponent);
	};
}
