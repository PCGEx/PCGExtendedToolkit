// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/External/PCGExMesh.h"
#include "Data/External/PCGExMeshCommon.h"
#include "Data/External/PCGExMeshImportDetails.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Fitting/PCGExFitting.h"
#include "Graphs/PCGExGraphDetails.h"
#include "PCGExMeshToClusters.generated.h"

namespace PCGExMesh
{
	class FGeoStaticMeshMap;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

UENUM()
enum class EPCGExMeshAttributeHandling : uint8
{
	StaticMeshSoftPath = 0 UMETA(DisplayName = "StaticMesh Soft Path", ToolTip="Read the attribute as a StaticMesh soft path."),
	ActorReference     = 1 UMETA(DisplayName = "Actor Reference", ToolTip="Read the attribute as an actor reference to extract primitive from."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/mesh-to-clusters"))
class UPCGExMeshToClustersSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(MeshToClusters, "Mesh to Clusters", "Creates clusters from mesh topology.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainInputPin() const override
	{
		return PCGExCommon::Labels::SourceTargetsLabel;
	}

	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	virtual bool GetMainAcceptMultipleData() const override
	{
		return false;
	}

	//~End UPCGExPointsProcessorSettings

	/** Triangulation type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExTriangulationType GraphOutputType = EPCGExTriangulationType::Raw;

	/** Mesh source. Attribute path can be FString, FName or FSoftObjectPath. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Static Mesh", AllowedClasses="/Script/Engine.StaticMesh"))
	FPCGExInputShorthandNameSoftObjectPath StaticMesh = FPCGExInputShorthandNameSoftObjectPath(FName("Mesh"));

	/** Static mesh path attribute type*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExMeshAttributeHandling AttributeHandling; // TODO : Refactor this to support both. We care about primitives, not where they come from.

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType StaticMeshInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName StaticMeshAttribute_DEPRECATED = "Mesh";

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	TSoftObjectPtr<UStaticMesh> StaticMeshConstant_DEPRECATED;

#pragma endregion

	/** Target inherit behavior */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTransformDetails TransformDetails;

	/** Which data should be imported from the static mesh onto the generated points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeoMeshImportDetails ImportDetails;

	/** Skip invalid meshes & do not throw warning about them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnoreMeshWarnings = false;

	/**
	 * Set tolerance for merging vertices, such as those found at split vertices along hard edges or UV seams.
	 * The value is clamped to be no less than a small positive value to prevent division by zero errors.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	float VertexMergeHashTolerance = static_cast<float>(PCGExMesh::DefaultVertexMergeHashTolerance);

	/**
	 * Use two overlapping spatial hashes to detect vertex proximity. True (default) is more accurate but
	 * slightly slower and uses slightly more memory during processing. (Specifically, the overhead is two
	 * hash lookups versus one per vertex, and memory overhead is on the order of 2 to 3 MB for 100K vertices.)
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable), AdvancedDisplay)
	bool bPreciseVertexMerge = false;

	/** Graph & Edges output properties. Only available if bPruneOutsideBounds as it otherwise generates a complete graph. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Which input points attributes to forward on clusters. NOTE : Not implemented */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExForwardDetails AttributesForwarding;

private:
	friend class FPCGExMeshToClustersElement;
};

struct FPCGExMeshToClustersContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExMeshToClustersElement;

	FPCGExGraphBuilderDetails GraphBuilderDetails;
	FPCGExTransformDetails TransformDetails;
	FPCGExGeoMeshImportDetails ImportDetails;

	TSharedPtr<PCGExData::FFacade> TargetsDataFacade;
	TSharedPtr<PCGExMesh::FGeoStaticMeshMap> StaticMeshMap;
	TArray<int32> MeshIdx;

	TSharedPtr<PCGExData::FPointIOCollection> RootVtx;
	TSharedPtr<PCGExData::FPointIOCollection> VtxChildCollection;
	TSharedPtr<PCGExData::FPointIOCollection> EdgeChildCollection;
	TSharedPtr<PCGExData::FPointIOCollection> BaseMeshDataCollection;

	TArray<TSharedPtr<PCGExGraphs::FGraphBuilder>> GraphBuilders;

	bool bWantsImport = false;
};


class FPCGExMeshToClustersElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(MeshToClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return true;
	}
};
