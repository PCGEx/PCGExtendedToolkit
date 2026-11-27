// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicDistance.h"
#include "Core/PCGExHeuristicOperation.h"
#include "UObject/Object.h"

#include "PCGExHeuristicSteepness.generated.h"

USTRUCT(BlueprintType)
struct FPCGExHeuristicConfigSteepness : public FPCGExHeuristicConfigBase
{
	GENERATED_BODY()

	FPCGExHeuristicConfigSteepness()
		: FPCGExHeuristicConfigBase()
	{
	}

	/** Accumulate steepness over multiple previous edges for smoother terrain evaluation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bAccumulateScore = false;

	/** How many previous edges should be added to the current score. Use this when dealing with very smooth terrain to exacerbate steepness. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bAccumulateScore", ClampMin=1))
	int32 AccumulationSamples = 1;

	/** Vector pointing in the "up" direction. Mirrored. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector UpVector = FVector::UpVector;

	/** When enabled, the overall steepness (whether toward or away the UpVector) determine the score. When disabled, the full range of the dot is used, with -1:1 remapped to 0:1 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bAbsoluteSteepness = true;
};

/**
 * 
 */
class FPCGExHeuristicSteepness : public FPCGExHeuristicOperation
{
	friend class UPCGExHeuristicsFactorySteepness;

public:
	// Without accumulation the edge score only reads From/To positions; accumulation walks the travel stack.
	virtual bool HasStaticEdgeScore() const override
	{
		return !bAccumulate;
	}

	// A single sample is the edge itself: the walk only adds history from the second sample on.
	virtual bool UsesTravelHistory() const override
	{
		return bAccumulate && MaxSamples > 1;
	}

	virtual void PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster) override;

	virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const override;

	virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const override;

protected:
	bool bAccumulate = false;
	int32 MaxSamples = 1;

	FVector UpwardVector = FVector::UpVector;
	bool bAbsoluteSteepness = true;

	double GetDot(const FVector& From, const FVector& To) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExHeuristicsFactorySteepness : public UPCGExHeuristicsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExHeuristicConfigSteepness Config;

	virtual TSharedPtr<FPCGExHeuristicOperation> CreateOperation(FPCGExContext* InContext) const override;
	PCGEX_HEURISTIC_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/heuristics/heuristics-steepness"))
class UPCGExHeuristicsSteepnessProviderSettings : public UPCGExHeuristicsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(HeuristicsSteepness, "Heuristics : Steepness", "Heuristics based on steepness.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExHeuristicConfigSteepness Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual const FPCGExHeuristicConfigBase* GetBaseConfig() const override { return &Config; }
#endif
};
