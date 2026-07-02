// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExChainGating.generated.h"

namespace PCGExClusters
{
	class FCluster;
	class FNodeChain;
}

UENUM()
enum class EPCGExChainGatingLogic : uint8
{
	All = 0 UMETA(DisplayName = "All (AND)", ToolTip="A chain matches only if every enabled criterion is satisfied."),
	Any = 1 UMETA(DisplayName = "Any (OR)", ToolTip="A chain matches if at least one enabled criterion is satisfied."),
};

namespace PCGExClusters
{
	/** Selects which optional angle metrics ComputeChainMetrics evaluates. Vtx/edge counts and length are always computed. */
	enum class EChainMetricFlags : uint8
	{
		None              = 0,
		CornerAngle       = 1 << 0,
		TotalCurvature    = 1 << 1,
		EndpointDeviation = 1 << 2,
	};

	ENUM_CLASS_FLAGS(EChainMetricFlags)

	/** Aggregate geometric metrics for a node chain, all computed in a single walk. */
	struct PCGEXGRAPHS_API FChainMetrics
	{
		/** Number of vertices (nodes) in the chain. */
		int32 VtxCount = 0;

		/** Number of edges in the chain. */
		int32 EdgeCount = 0;

		/** Summed length of the chain. */
		double Length = 0;

		/** Sharpest corner (largest deflection between consecutive edges), in degrees. 0 = perfectly straight, 180 = full reversal. */
		double SharpestCorner = 0;

		/** Sum of every corner deflection along the chain, in degrees. Accumulates with each bend regardless of turn direction. */
		double TotalCurvature = 0;

		/** Angle between the chain's first and last edge direction, in degrees. 0 = ends aligned (straight, or a symmetric wiggle). Always 0 for closed loops, which have no endpoints. */
		double EndpointDeviation = 0;
	};

	/** Computes vertex/edge counts and length always; angle metrics only for the requested flags. Single walk. */
	PCGEXGRAPHS_API void ComputeChainMetrics(
		const FNodeChain& Chain,
		const FCluster& Cluster,
		FChainMetrics& OutMetrics,
		EChainMetricFlags Flags = EChainMetricFlags::None);
}

/**
 * Reusable gating criteria for node chains.
 * Selects chains on vtx/edge counts, length and corner/curvature metrics, combined with an AND/OR logic.
 * Designed to be embedded by any chain-consuming operation (refinements, simplification, ...).
 */
USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExChainGatingDetails
{
	GENERATED_BODY()

	FPCGExChainGatingDetails() = default;

	/** How enabled criteria are combined to decide whether a chain matches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExChainGatingLogic Logic = EPCGExChainGatingLogic::All;

	/** Gate on a lower bound for the vertex (node) count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinVtxCount = false;

	/** Inclusive lower bound for the vertex count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Vtx Count", EditCondition="bCheckMinVtxCount", ClampMin=0))
	int32 MinVtxCount = 0;

	/** Gate on an upper bound for the vertex (node) count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxVtxCount = false;

	/** Inclusive upper bound for the vertex count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Vtx Count", EditCondition="bCheckMaxVtxCount", ClampMin=0))
	int32 MaxVtxCount = 100;

	/** Gate on a lower bound for the edge count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinEdgeCount = false;

	/** Inclusive lower bound for the edge count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Edge Count", EditCondition="bCheckMinEdgeCount", ClampMin=0))
	int32 MinEdgeCount = 0;

	/** Gate on an upper bound for the edge count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxEdgeCount = false;

	/** Inclusive upper bound for the edge count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Edge Count", EditCondition="bCheckMaxEdgeCount", ClampMin=0))
	int32 MaxEdgeCount = 100;

	/** Gate on a lower bound for the total chain length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinLength = false;

	/** Inclusive lower bound for the chain length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Length", EditCondition="bCheckMinLength", ClampMin=0))
	double MinLength = 0;

	/** Gate on an upper bound for the total chain length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxLength = false;

	/** Inclusive upper bound for the chain length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Length", EditCondition="bCheckMaxLength", ClampMin=0))
	double MaxLength = 1000;

	/** Gate on a lower bound for the sharpest corner (largest deflection between consecutive edges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinCornerAngle = false;

	/** Inclusive lower bound for the sharpest corner. Selects chains that bend at least this much somewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Corner Angle", EditCondition="bCheckMinCornerAngle", ClampMin=0, ClampMax=180, Units="Degrees"))
	double MinCornerAngle = 0;

	/** Gate on an upper bound for the sharpest corner (largest deflection between consecutive edges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxCornerAngle = false;

	/** Inclusive upper bound for the sharpest corner. A single corner sharper than this fails the chain (keeps it 'straight-ish'). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Corner Angle", EditCondition="bCheckMaxCornerAngle", ClampMin=0, ClampMax=180, Units="Degrees"))
	double MaxCornerAngle = 45;

	/** Gate on a lower bound for the total accumulated curvature (sum of all corner deflections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinTotalCurvature = false;

	/** Inclusive lower bound for the summed deflection over the whole chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Total Curvature", EditCondition="bCheckMinTotalCurvature", ClampMin=0, Units="Degrees"))
	double MinTotalCurvature = 0;

	/** Gate on an upper bound for the total accumulated curvature (sum of all corner deflections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxTotalCurvature = false;

	/** Inclusive upper bound for the summed deflection. A squiggle accumulates here even when it stays 'straight-ish' overall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Total Curvature", EditCondition="bCheckMaxTotalCurvature", ClampMin=0, Units="Degrees"))
	double MaxTotalCurvature = 90;

	/** Gate on a lower bound for the endpoint deviation (angle between the first and last edge direction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMinEndpointDeviation = false;

	/** Inclusive lower bound for the net change of heading between the chain's ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ┌─ Min Endpoint Deviation", EditCondition="bCheckMinEndpointDeviation", ClampMin=0, ClampMax=180, Units="Degrees"))
	double MinEndpointDeviation = 0;

	/** Gate on an upper bound for the endpoint deviation (angle between the first and last edge direction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(InlineEditConditionToggle))
	bool bCheckMaxEndpointDeviation = false;

	/** Inclusive upper bound for the net heading change. A symmetric wiggle stays low here; a consistent curve does not. Closed loops report 0 (no endpoints). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Max Endpoint Deviation", EditCondition="bCheckMaxEndpointDeviation", ClampMin=0, ClampMax=180, Units="Degrees"))
	double MaxEndpointDeviation = 30;

	/** Whether any criterion is enabled at all. */
	bool IsEnabled() const;

	/** Returns true if the chain matches the enabled criteria under the current logic. Returns false when no criterion is enabled. */
	bool Test(const PCGExClusters::FNodeChain& Chain, const PCGExClusters::FCluster& Cluster) const;
};
