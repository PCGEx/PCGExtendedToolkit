// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExVtxPropertyFactoryProvider.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExVtxPropertySpecialNeighbors.generated.h"

USTRUCT(BlueprintType)
struct FPCGExSpecialNeighborsConfig
{
	GENERATED_BODY()

	/** The neighbor holding the most edges of its own. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExEdgeOutputWithIndexSettings LargestNeighbor = FPCGExEdgeOutputWithIndexSettings(TEXT("Largest"));

	/** The neighbor holding the fewest edges of its own. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExEdgeOutputWithIndexSettings SmallestNeighbor = FPCGExEdgeOutputWithIndexSettings(TEXT("Smallest"));
};

/**
 * 
 */
class FPCGExVtxPropertySpecialNeighbors : public FPCGExVtxPropertyOperation
{
public:
	FPCGExSpecialNeighborsConfig Config;

	virtual bool PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual void ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExVtxPropertySpecialNeighborsFactory : public UPCGExVtxPropertyFactoryData
{
	GENERATED_BODY()

public:
	FPCGExSpecialNeighborsConfig Config;
	virtual TSharedPtr<FPCGExVtxPropertyOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|VtxProperty", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-vtx-properties/vtx-special-neighbors"))
class UPCGExVtxPropertySpecialNeighborsSettings : public UPCGExVtxPropertyProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(VtxSpecialNeighbors, "Vtx : Special Neighbors", "Fetch data from neighbors")
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Direction Settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSpecialNeighborsConfig Config;
};
