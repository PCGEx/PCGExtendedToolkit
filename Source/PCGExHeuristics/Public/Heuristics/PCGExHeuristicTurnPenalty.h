// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExHeuristicOperation.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "UObject/Object.h"
#include "PCGExHeuristicTurnPenalty.generated.h"

USTRUCT(BlueprintType)
struct FPCGExHeuristicConfigTurnPenalty : public FPCGExHeuristicConfigBase
{
	GENERATED_BODY()

	FPCGExHeuristicConfigTurnPenalty()
		: FPCGExHeuristicConfigBase()
	{
	}

	/** Angle in degrees below which no penalty is applied (straight-ish paths). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, ClampMax=180, Units="Degrees"))
	double MinAngleThreshold = 0;

	/** Angle in degrees at which maximum penalty is applied. Angles above this are clamped. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, ClampMax=180, Units="Degrees"))
	double MaxAngleThreshold = 180;

	/** If enabled, use the absolute angle (treats left and right turns equally). If disabled, can distinguish turn direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bAbsoluteAngle = true;

	/** Value used for global score (initial A* sorting). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fallbacks", meta=(PCG_Overridable, DisplayPriority=-1, ClampMin=0, ClampMax=1))
	double GlobalScore = 0;

	/** Fallback score when no previous direction exists (first edge from seed). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fallbacks", meta=(PCG_Overridable, DisplayPriority=-1, ClampMin=0, ClampMax=1))
	double FallbackScore = 0;
};

/**
 * Heuristic that penalizes sharp turns based on the angle between consecutive edges.
 * Requires path history (TravelStack) to determine incoming direction.
 */
class FPCGExHeuristicTurnPenalty : public FPCGExHeuristicOperation
{
public:
	double MinAngleRad = 0;
	double MaxAngleRad = PI;
	double AngleRange = PI;
	bool bAbsoluteAngle = true;
	double GlobalScore = 0;
	double FallbackScore = 0;

	virtual bool UsesTravelHistory() const override
	{
		return true;
	}

	virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const override;

	virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const override;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExHeuristicsFactoryTurnPenalty : public UPCGExHeuristicsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExHeuristicConfigTurnPenalty Config;

	virtual TSharedPtr<FPCGExHeuristicOperation> CreateOperation(FPCGExContext* InContext) const override;
	PCGEX_HEURISTIC_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/heuristics/heuristics-turn-penalty"))
class UPCGExHeuristicsTurnPenaltyProviderSettings : public UPCGExHeuristicsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(HeuristicsTurnPenalty, "Heuristics : Turn Penalty", "Heuristics based on turn angle between consecutive edges.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExHeuristicConfigTurnPenalty Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual const FPCGExHeuristicConfigBase* GetBaseConfig() const override { return &Config; }
#endif
};
