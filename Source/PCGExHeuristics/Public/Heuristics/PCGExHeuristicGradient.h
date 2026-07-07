// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExHeuristicOperation.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "PCGExHeuristicGradient.generated.h"

class FPCGExHeuristicOperation;

UENUM()
enum class EPCGExGradientMode : uint8
{
	FollowIncreasing = 0 UMETA(DisplayName = "Follow Increasing", ToolTip="Prefer edges where attribute value increases (To > From). Lower score = better."),
	FollowDecreasing = 1 UMETA(DisplayName = "Follow Decreasing", ToolTip="Prefer edges where attribute value decreases (To < From). Lower score = better."),
	AvoidChange      = 2 UMETA(DisplayName = "Avoid Change", ToolTip="Prefer edges where attribute value stays similar. Penalize large changes."),
	SeekChange       = 3 UMETA(DisplayName = "Seek Change", ToolTip="Prefer edges where attribute value changes significantly."),
};

USTRUCT(BlueprintType)
struct FPCGExHeuristicGradientConfig : public FPCGExHeuristicConfigBase
{
	GENERATED_BODY()

	FPCGExHeuristicGradientConfig()
		: FPCGExHeuristicConfigBase()
	{
	}

	/** How to interpret the gradient between nodes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExGradientMode Mode = EPCGExGradientMode::FollowIncreasing;

	/** Attribute to read values from (must be on vertices). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	/** If enabled, normalize the gradient by edge length (gradient per unit distance). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bNormalizeByDistance = false;

	/** Expected minimum gradient value for normalization. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExGradientMode::AvoidChange || Mode == EPCGExGradientMode::SeekChange"))
	double MinGradient = 0;

	/** Expected maximum gradient value for normalization. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExGradientMode::AvoidChange || Mode == EPCGExGradientMode::SeekChange"))
	double MaxGradient = 1;
};

/**
 * Heuristic that scores edges based on attribute gradient (change in value).
 * Can follow increasing values, decreasing values, or penalize/seek change.
 */
class FPCGExHeuristicGradient : public FPCGExHeuristicOperation
{
public:
	EPCGExGradientMode Mode = EPCGExGradientMode::FollowIncreasing;
	FPCGAttributePropertyInputSelector Attribute;
	bool bNormalizeByDistance = false;
	double MinGradient = 0;
	double MaxGradient = 1;
	double GradientRange = 1;

	virtual bool HasStaticEdgeScore() const override
	{
		return true;
	}

	virtual void PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster) override;

	virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const override;

	virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const override;

protected:
	/** Cached attribute values per node (indexed by node index) */
	TArray<double> CachedValues;

	/** Reciprocal of the cluster's max edge length -- EdgeLengths are raw world-space, and
	 * bNormalizeByDistance divides by the max-relative length to keep gradients in a usable range. */
	double InvMaxEdgeLength = 1;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExHeuristicsFactoryGradient : public UPCGExHeuristicsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExHeuristicGradientConfig Config;

	virtual TSharedPtr<FPCGExHeuristicOperation> CreateOperation(FPCGExContext* InContext) const override;
	PCGEX_HEURISTIC_FACTORY_BOILERPLATE

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/heuristics/heuristics-gradient"))
class UPCGExHeuristicsGradientProviderSettings : public UPCGExHeuristicsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(HeuristicsGradient, "Heuristics : Gradient", "Heuristics based on attribute gradient between nodes.", FName(GetDisplayName()))

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_BLEND(Heuristics, ColorMisc);
	}

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Gradient Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExHeuristicGradientConfig Config;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
