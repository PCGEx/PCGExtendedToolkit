// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExPointData.h"
#include "Factories/PCGExFactories.h" // 5.6
#include "PCGExClusterData.generated.h"

namespace PCGExData
{
	enum class EIOInit : uint8;
}

namespace PCGExClusters
{
	class FCluster;
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Cluster Part"))
struct FPCGExDataTypeInfoClusterPart : public FPCGDataTypeInfoPoint
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCORE_API)

#if WITH_EDITOR
	virtual bool Hidden() const override;
#endif // WITH_EDITOR
};

/**
 * 
 */
UCLASS(Abstract)
class PCGEXCORE_API UPCGExClusterData : public UPCGExPointData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoClusterPart)
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Cluster Vtx"))
struct FPCGExDataTypeInfoVtx : public FPCGExDataTypeInfoClusterPart
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCORE_API)
};

/**
 * 
 */
UCLASS()
class PCGEXCORE_API UPCGExClusterNodesData : public UPCGExClusterData
{
	GENERATED_BODY()

protected:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoVtx)

	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Cluster Edges"))
struct FPCGExDataTypeInfoEdges : public FPCGExDataTypeInfoClusterPart
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCORE_API)
};

/**
 * 
 */
UCLASS()
class PCGEXCORE_API UPCGExClusterEdgesData : public UPCGExClusterData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoEdges)

	virtual void InitializeSpatialDataInternal(const FPCGInitializeFromDataParams& InParams) override;

	virtual void SetBoundCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster);
	const TSharedPtr<PCGExClusters::FCluster>& GetBoundCluster() const;

	virtual void BeginDestroy() override;

protected:
	TSharedPtr<PCGExClusters::FCluster> Cluster;

	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
};
