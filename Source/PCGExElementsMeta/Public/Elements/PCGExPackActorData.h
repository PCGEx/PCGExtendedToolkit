// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Factories/PCGExInstancedFactory.h"

#include "Core/PCGExPointsProcessor.h"
#include "Helpers/PCGExBufferHelper.h"


#include "PCGExPackActorData.generated.h"

namespace PCGExMT
{
	class FAsyncToken;
	class FTimeSlicedMainThreadLoop;
}

namespace PCGExPackActorData
{
}

/**
 * Executed once per point
 */
UCLASS(Blueprintable, BlueprintType, Abstract, MinimalAPI, DisplayName = "[PCGEx] Custom Actor Data Packer", meta=(ToolTip = "Custom logic for packing actor component data onto PCG points."))
class UPCGExCustomActorDataPacker : public UPCGExInstancedFactory
{
	GENERATED_BODY()

	struct FComponentInfos
	{
		AActor* Owner = nullptr;
		UActorComponent* Component = nullptr;
		FAttachmentTransformRules AttachmentTransformRules;

		FComponentInfos()
			: AttachmentTransformRules(FAttachmentTransformRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false))
		{
		}

		FComponentInfos(UActorComponent* InComponent, EAttachmentRule InLocationRule, EAttachmentRule InRotationRule, EAttachmentRule InScaleRule, bool InWeldSimulatedBodies)
			: Component(InComponent)
			  , AttachmentTransformRules(FAttachmentTransformRules(InLocationRule, InRotationRule, InScaleRule, InWeldSimulatedBodies))
		{
		}
	};

public:
	virtual bool WantsPerDataInstance() override
	{
		return true;
	}

	TSharedPtr<FPCGExUniqueNameGenerator> UniqueNameGenerator;
	bool bIsPreviewMode = false;
	bool bIsProcessing = false;

	/**
	 * Main initialization function. Called once, and is responsible for populating graph builder settings.
	 * At least one setting is expected to be found in the GraphSettings array. This is executed on the main thread.
	 * @param InContext - Context of the initialization
	 * @param OutSuccess
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void Initialize(bool& OutSuccess);

	/**
	 * Process an actor reference. This method is executed in a multi-threaded context
	 * @param InActor The actor to be processed.
	 * @param InPoint PCG point that correspond to the provided actor.
	 * @param InPointIndex The point index
	 * @param OutPoint Muted PCG Point that correspond to the actor
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void ProcessEntry(AActor* InActor, const FPCGPoint& InPoint, const int32 InPointIndex, FPCGPoint& OutPoint);

	/**
	 * Create a component that will be attached to the actor at the end of the execution.
	 * @param InActor Actor to which the component will be attached
	 * @param ComponentClass Component class
	 * @param InLocationRule The rule to apply to location when attaching
	 * @param InRotationRule The rule to apply to rotation when attaching
	 * @param InScaleRule The rule to apply to scale when attaching
	 * @param InWeldSimulatedBodies Whether to weld simulated bodies together when attaching
	 * @param OutComponent Created Component
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Execution", meta=(DeterminesOutputType="ComponentClass", DynamicOutputParam="OutComponent"))
	void AddComponent(AActor* InActor, UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UActorComponent> ComponentClass, EAttachmentRule InLocationRule, EAttachmentRule InRotationRule, EAttachmentRule InScaleRule, bool InWeldSimulatedBodies, UActorComponent*& OutComponent);


	virtual void Cleanup() override
	{
		InputActors.Empty();
		Super::Cleanup();
	}

	UPROPERTY(BlueprintReadOnly, Category = "PCGEx|Inputs")
	TArray<TObjectPtr<AActor>> InputActors;

	TSet<FSoftObjectPath> RequiredAssetsPaths;
	TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;

	TSharedPtr<PCGExData::TBufferHelper<PCGExData::EBufferHelperMode::Write>> WriteBuffers;
	TSharedPtr<PCGExData::TBufferHelper<PCGExData::EBufferHelperMode::Read>> ReadBuffers;

#pragma region Init

	/** Enable this if you're spawning components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bExecuteOnMainThread = false;

	/**
	 * Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitInt32(const FName& InAttributeName, const int32& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitInt64(const FName& InAttributeName, const int64& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitFloat(const FName& InAttributeName, const float& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitDouble(const FName& InAttributeName, const double& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitVector2(const FName& InAttributeName, const FVector2D& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitVector(const FName& InAttributeName, const FVector& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitVector4(const FName& InAttributeName, const FVector4& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitQuat(const FName& InAttributeName, const FQuat& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitTransform(const FName& InAttributeName, const FTransform& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitString(const FName& InAttributeName, const FString& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitBool(const FName& InAttributeName, const bool& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitRotator(const FName& InAttributeName, const FRotator& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitName(const FName& InAttributeName, const FName& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Init", meta=(InAttributeName="None"))
	bool InitSoftObjectPath(const FName& InAttributeName, const FSoftObjectPath& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	bool InitSoftClassPath(const FName& InAttributeName, const FSoftClassPath& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Initialization", meta=(InAttributeName="None"))
	void PreloadObjectPaths(const FName& InAttributeName);

#pragma endregion

#pragma region Setters

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteInt32(const FName& InAttributeName, const int32 InPointIndex, const int32& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteInt64(const FName& InAttributeName, const int32 InPointIndex, const int64& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteFloat(const FName& InAttributeName, const int32 InPointIndex, const float& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteDouble(const FName& InAttributeName, const int32 InPointIndex, const double& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteVector2(const FName& InAttributeName, const int32 InPointIndex, const FVector2D& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteVector(const FName& InAttributeName, const int32 InPointIndex, const FVector& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=0))
	bool WriteVector4(const FName& InAttributeName, const int32 InPointIndex, const FVector4& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None"))
	bool WriteQuat(const FName& InAttributeName, const int32 InPointIndex, const FQuat& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None"))
	bool WriteTransform(const FName& InAttributeName, const int32 InPointIndex, const FTransform& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue="Value"))
	bool WriteString(const FName& InAttributeName, const int32 InPointIndex, const FString& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue=false))
	bool WriteBool(const FName& InAttributeName, const int32 InPointIndex, const bool& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None"))
	bool WriteRotator(const FName& InAttributeName, const int32 InPointIndex, const FRotator& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None", InValue="None"))
	bool WriteName(const FName& InAttributeName, const int32 InPointIndex, const FName& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None"))
	bool WriteSoftObjectPath(const FName& InAttributeName, const int32 InPointIndex, const FSoftObjectPath& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter", meta=(InAttributeName="None"))
	bool WriteSoftClassPath(const FName& InAttributeName, const int32 InPointIndex, const FSoftClassPath& InValue);

#pragma endregion

#pragma region Getters

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadInt32(const FName& InAttributeName, const int32 InPointIndex, int32& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadInt64(const FName& InAttributeName, const int32 InPointIndex, int64& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadFloat(const FName& InAttributeName, const int32 InPointIndex, float& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadDouble(const FName& InAttributeName, const int32 InPointIndex, double& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadVector2(const FName& InAttributeName, const int32 InPointIndex, FVector2D& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadVector(const FName& InAttributeName, const int32 InPointIndex, FVector& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadVector4(const FName& InAttributeName, const int32 InPointIndex, FVector4& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadQuat(const FName& InAttributeName, const int32 InPointIndex, FQuat& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadTransform(const FName& InAttributeName, const int32 InPointIndex, FTransform& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadString(const FName& InAttributeName, const int32 InPointIndex, FString& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadBool(const FName& InAttributeName, const int32 InPointIndex, bool& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadRotator(const FName& InAttributeName, const int32 InPointIndex, FRotator& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadName(const FName& InAttributeName, const int32 InPointIndex, FName& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadSoftObjectPath(const FName& InAttributeName, const int32 InPointIndex, FSoftObjectPath& OutValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InPointIndex The point index to set the value to.
	 * @param OutValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(InAttributeName="None"))
	bool ReadSoftClassPath(const FName& InAttributeName, const int32 InPointIndex, FSoftClassPath& OutValue);

	/**
	 * Create a component that will be attached to the actor at the end of the execution.
	 * @param InAttributeName 
	 * @param InPointIndex 
	 * @param ObjectClass 
	 * @param OutObject 
	 * @param OutIsValid 
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Getter", meta=(DeterminesOutputType="ObjectClass", DynamicOutputParam="OutObject"))
	void ResolveObjectPath(const FName& InAttributeName, const int32 InPointIndex, UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ObjectClass, UObject*& OutObject, bool& OutIsValid);

#pragma endregion
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="metadata/keys/pack-actor-data/creating-a-custom-actor-data-packer"))
class UPCGExPackActorDataSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExPackActorDataSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(PackActorData, "Pack Actor Data", "Use custom blueprint to read data from actor references.", (Packer ? FName(Packer.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return bTrackActors;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings

public:
	virtual FName GetMainInputPin() const override;
	//~End UPCGExPointsProcessorSettings

	/** Actor reference */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName ActorReferenceAttribute = FName(TEXT("ActorReference"));

	/** Builder instance. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExCustomActorDataPacker> Packer;

	/** Skip entries where the actor reference could not be resolved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOmitUnresolvedEntries = true;

	/** Skip outputting empty data collections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOmitEmptyOutputs = true;

	/** When enabled, will track referenced actors and trigger a graph regeneration when their properties change. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bTrackActors = true;

	/** If enabled, will turn off uninitialized packer warning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bQuietUninitializedPackerWarning = false;
};

struct FPCGExPackActorDataContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExPackActorDataElement;
	UPCGExCustomActorDataPacker* Packer = nullptr;
	//TArray<UPCGParamData*> OutputParams;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPackActorDataElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true) // packs into actor components on the game thread; keep output off the worker/paused path
	PCGEX_ELEMENT_CREATE_CONTEXT(PackActorData)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPackActorData
{
	const FName SourceOverridesPacker = TEXT("Overrides : Packer");

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPackActorDataContext, UPCGExPackActorDataSettings>
	{
		TArray<FPCGMetadataAttributeBase*> Attributes;
		UPCGExCustomActorDataPacker* Packer = nullptr;

		TSet<AActor*> UniqueActors;
		TSharedPtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> ActorReferences;

		TArray<FPCGPoint> PointsForProcessing;
		TArray<int8> PointMask;

		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;
		TSharedPtr<FStreamableHandle> LoadHandle;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void StartProcessing();

		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
