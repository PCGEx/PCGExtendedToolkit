// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExHeuristicOperation.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "UObject/Object.h"
#include "PCGExHeuristicAzimuth.generated.h"

USTRUCT(BlueprintType)
struct FPCGExHeuristicConfigAzimuth : public FPCGExHeuristicConfigBase
{
	GENERATED_BODY()

	FPCGExHeuristicConfigAzimuth()
		: FPCGExHeuristicConfigBase()
	{
	}
};

/**
 *
 */
class FPCGExHeuristicAzimuth : public FPCGExHeuristicOperation
{
	/** Cached edge directions for fast lookup (indexed by edge index) */
	TArray<FVector> CachedEdgeDirections;

public:
	virtual void PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster) override;

	virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const override;

	virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const override;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExHeuristicsFactoryAzimuth : public UPCGExHeuristicsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExHeuristicConfigAzimuth Config;

	virtual TSharedPtr<FPCGExHeuristicOperation> CreateOperation(FPCGExContext* InContext) const override;
	PCGEX_HEURISTIC_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/heuristics/heuristics-azimuth"))
class UPCGExHeuristicsAzimuthProviderSettings : public UPCGExHeuristicsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(HeuristicsAzimuth, "Heuristics : Azimuth", "Heuristics based on direction toward final goal (north star).", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExHeuristicConfigAzimuth Config;


#if WITH_EDITOR
	virtual const FPCGExHeuristicConfigBase* GetBaseConfig() const override { return &Config; }
#endif
};
