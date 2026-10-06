// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Helpers/PCGExBufferHelper.h"
#include "PCGExBuildCustomGraph.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

UENUM()
enum class EPCGExCustomGraphActorSourceMode : uint8
{
	Owner           = 0 UMETA(DisplayName = "Owner", ToolTip="PCG Component owner"),
	ActorReferences = 1 UMETA(DisplayName = "Actor References", ToolTip="Point data with an actor reference property."),
};

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, Abstract, DisplayName = "[PCGEx] Custom Graph Settings", meta=(PCGExNodeLibraryDoc="clusters/build-custom-graph"))
class PCGEXELEMENTSCLUSTERS_API UPCGExCustomGraphSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Internal index of these settings. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "PCGEx|Data")
	int32 SettingsIndex = 0;

	TArray<int64> Idx;
	TMap<int64, int32> IdxMap;
	TSharedPtr<TArray<int32>> ValidNodeIndices;
	TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

	TSet<uint64> UniqueEdges;

	int32 GetOrCreateNode(int64 InIdx);

	/**
	 * Creates an edge between two nodes in an indexed graph.
	 * @param InStartIdx 
	 * @param InEndIdx 
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Data")
	void AddEdge(const int64 InStartIdx, const int64 InEndIdx);

	/**
	 * Removes an edge between two nodes in an indexed graph.
	 * @param InStartIdx 
	 * @param InEndIdx 
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Data")
	void RemoveEdge(const int64 InStartIdx, const int64 InEndIdx);

	/**
	 * Initialization method. It is called right before Build Graph -- this is where you must set the max number of nodes.
	 * @param OutSuccess The maximum number of node this graph will be working with.
	 * @param OutNodeReserve Number of nodes to reserve. This is mostly for memory optimization purpose. Try to be as close as possible if you can; slightly more is better than slightly less.
	 * @param OutEdgeReserve Number of edges to reserve. This is mostly for memory optimization purpose. Try to be as close as possible if you can; slightly more is better than slightly less.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void InitializeSettings(bool& OutSuccess, int32& OutNodeReserve, int32& OutEdgeReserve);

	/**
	 * Main execution function. Called once per requested graphs. This method is executed in a multi-threaded context, Graph Settings are safe but the custom builder wrapper itself isn't.
	 * @param OutSuccess Whether building was successful or not
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void BuildGraph(bool& OutSuccess);

	/**
	 * This function is called after BuildGraph, when the point metadata has been initialized, so you can initialized default attribute values here.
	 * Non-initialized attribute will still work, but the default value under the hood will be the first one set, which is not deterministic due to the multhreaded nature of the processing.
	 * @param OutSuccess Whether initialization was successful or not
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void InitPointAttributes(bool& OutSuccess);

	/**
	 * Update Node Point is called on each node point after BuildGraph has been, and edges added. This method is executed in a multi-threaded context.
	 * This is where point transform & properties should be set.
	 * @param InPoint PCG Point that represents the node
	 * @param InNodeIdx Index of the node the given point matches with
	 * @param InPointIndex Index of the node' PCG Point (before pruning)
	 * @param OutPoint Muted PCG Point that represents the node.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void UpdateNodePoint(const FPCGPoint& InPoint, int64 InNodeIdx, int32 InPointIndex, FPCGPoint& OutPoint);

#pragma region Node Attributes

	TSharedPtr<PCGExData::TBufferHelper<PCGExData::EBufferHelperMode::Write>> VtxBuffers;

#pragma region Init

	/**
	 * Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeInt32(const FName& InAttributeName, const int32& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeInt64(const FName& InAttributeName, const int64& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeFloat(const FName& InAttributeName, const float& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeDouble(const FName& InAttributeName, const double& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeVector2(const FName& InAttributeName, const FVector2D& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeVector(const FName& InAttributeName, const FVector& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeVector4(const FName& InAttributeName, const FVector4& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeQuat(const FName& InAttributeName, const FQuat& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Execution")
	bool InitNodeTransform(const FName& InAttributeName, const FTransform& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeString(const FName& InAttributeName, const FString& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeBool(const FName& InAttributeName, const bool& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeRotator(const FName& InAttributeName, const FRotator& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeName(const FName& InAttributeName, const FName& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeSoftObjectPath(const FName& InAttributeName, const FSoftObjectPath& InValue);

	/**
	* Initialize a point' attribute default value.
	 * Must be called during initialization.
	 * @param InAttributeName
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool InitNodeSoftClassPath(const FName& InAttributeName, const FSoftClassPath& InValue);

#pragma endregion

#pragma region Setters

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeInt32(const FName& InAttributeName, const int64 InNodeIdx, const int32& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeInt64(const FName& InAttributeName, const int64 InNodeIdx, const int64& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeFloat(const FName& InAttributeName, const int64 InNodeIdx, const float& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeDouble(const FName& InAttributeName, const int64 InNodeIdx, const double& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeVector2(const FName& InAttributeName, const int64 InNodeIdx, const FVector2D& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeVector(const FName& InAttributeName, const int64 InNodeIdx, const FVector& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeVector4(const FName& InAttributeName, const int64 InNodeIdx, const FVector4& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeQuat(const FName& InAttributeName, const int64 InNodeIdx, const FQuat& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Execution")
	bool SetNodeTransform(const FName& InAttributeName, const int64 InNodeIdx, const FTransform& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeString(const FName& InAttributeName, const int64 InNodeIdx, const FString& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeBool(const FName& InAttributeName, const int64 InNodeIdx, const bool& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeRotator(const FName& InAttributeName, const int64 InNodeIdx, const FRotator& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeName(const FName& InAttributeName, const int64 InNodeIdx, const FName& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeSoftObjectPath(const FName& InAttributeName, const int64 InNodeIdx, const FSoftObjectPath& InValue);

	/**
	 * Set a point' attribute value at a given index.
	 * @param InAttributeName
	 * @param InNodeIdx The node ID to set the value to.
	 * @param InValue
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Setter")
	bool SetNodeSoftClassPath(const FName& InAttributeName, const int64 InNodeIdx, const FSoftClassPath& InValue);

#pragma endregion

#pragma endregion
};

USTRUCT(BlueprintType)
struct FNewGraphSettingsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Result")
	bool bIsValid = false;

	UPROPERTY(BlueprintReadWrite, Category = "Result")
	TObjectPtr<UPCGExCustomGraphSettings> Settings = nullptr;
};

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, Abstract, DisplayName = "[PCGEx] Custom Graph Builder")
class PCGEXELEMENTSCLUSTERS_API UPCGExCustomGraphBuilder : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsPerDataInstance() override
	{
		return true;
	}

	/**
	 * Main initialization function. Called once, and is responsible for populating graph builder settings.
	 * At least one setting is expected to be found in the GraphSettings array. This is executed on the main thread.
	 * @param OutSuccess
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void Initialize(bool& OutSuccess);

	/**
	 * Create a Graph Setting object that will be processed individually and generate its own cluster(s)
	 * @param SettingsClass
	 * @param OutSettings
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Execution", meta=(DeterminesOutputType="SettingsClass", DynamicOutputParam="OutSettings"))
	void CreateGraphSettings(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UPCGExCustomGraphSettings> SettingsClass, UPCGExCustomGraphSettings*& OutSettings);

	/**
	 * Main execution function. Called once per requested graphs. This method is executed in a multi-threaded context, Graph Settings are safe but the custom builder wrapper itself isn't.
	 * @param InCustomGraphSettings
	 * @param OutSuccess
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Execution")
	void BuildGraph(UPCGExCustomGraphSettings* InCustomGraphSettings, bool& OutSuccess);

	virtual void Cleanup() override
	{
		InputActors.Empty();

		for (UPCGExCustomGraphSettings* GSettings : GraphSettings)
		{
			GSettings->GraphBuilder.Reset();
		}

		GraphSettings.Empty();
		Super::Cleanup();
	}

	UPROPERTY(BlueprintReadOnly, Category = "PCGEx|Inputs")
	TArray<TObjectPtr<AActor>> InputActors;

	UPROPERTY(BlueprintReadOnly, Category = "PCGEx|Outputs")
	TArray<TObjectPtr<UPCGExCustomGraphSettings>> GraphSettings;

	UPROPERTY(BlueprintReadOnly, Category = "PCGEx|Inputs")
	bool DoEdgeAttributeStep = false;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters")
class UPCGExBuildCustomGraphSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(BuildCustomGraph, "Cluster : Build Custom Graph", "Create clusters using custom blueprint objects", (Builder ? FName(Builder.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	virtual bool IsInputless() const override
	{
		return Mode == EPCGExCustomGraphActorSourceMode::Owner;
	}

	//~End UPCGExPointsProcessorSettings

	/** Actor fetching mode. These actors will be forwarded to the builder so it can fetch components and data from there during its initialization. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCustomGraphActorSourceMode Mode = EPCGExCustomGraphActorSourceMode::Owner;

	/** Actor reference */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Mode == EPCGExCustomGraphActorSourceMode::ActorReferences", EditConditionHides))
	FName ActorReferenceAttribute = FName(TEXT("ActorReference"));

	/** Builder instance. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExCustomGraphBuilder> Builder;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Suppress warnings when graph settings remain unprocessed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietUnprocessedSettingsWarning = false;

	/** Suppress warnings when BuildGraph returns false. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietFailedBuildGraphWarning = false;
};

struct FPCGExBuildCustomGraphContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBuildCustomGraphElement;
	UPCGExCustomGraphBuilder* Builder = nullptr;
};

class FPCGExBuildCustomGraphElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildCustomGraph)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildCustomGraph
{
	const FName SourceOverridesBuilder = TEXT("Overrides : Graph Builder");
}
