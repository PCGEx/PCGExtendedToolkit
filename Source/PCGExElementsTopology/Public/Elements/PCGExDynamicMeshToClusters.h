// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Data/External/PCGExMesh.h"
#include "Data/External/PCGExMeshCommon.h"
#include "Data/External/PCGExMeshImportDetails.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExDynamicMeshToClusters.generated.h"

namespace PCGExData
{
	class FPointIOCollection;
}

namespace PCGExMesh
{
	class FGeoDynMesh;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/dynamic-mesh-to-clusters"))
class UPCGExDynamicMeshToClustersSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(DynamicMeshToClusters, "Dynamic Mesh to Clusters", "Creates clusters from dynamic mesh topology.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Triangulation type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExTriangulationType GraphOutputType = EPCGExTriangulationType::Raw;

	/** Which data should be imported from the dynamic mesh onto the generated points */
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
	 * Use two overlapping spatial hashes to detect vertex proximity. More accurate, but slightly slower and
	 * uses slightly more memory during processing.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable), AdvancedDisplay)
	bool bPreciseVertexMerge = false;

	/** Graph & Edges output properties. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

private:
	friend class FPCGExDynamicMeshToClustersElement;
};

struct FPCGExDynamicMeshToClustersContext final : FPCGExContext
{
	friend class FPCGExDynamicMeshToClustersElement;

	FPCGExGraphBuilderDetails GraphBuilderDetails;
	FPCGExGeoMeshImportDetails ImportDetails;
	bool bWantsImport = false;

	TSharedPtr<PCGExData::FPointIOCollection> VtxCollection;
	TSharedPtr<PCGExData::FPointIOCollection> EdgeCollection;

	TArray<TSharedPtr<PCGExGraphs::FGraphBuilder>> GraphBuilders;

	/** Tags from each input mesh, indexed by input order. */
	TArray<FPCGTaggedData> InputEntries;
};

class FPCGExDynamicMeshToClustersElement final : public IPCGExElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return true;
	}

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(DynamicMeshToClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
