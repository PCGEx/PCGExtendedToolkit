// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExClusterCommon.generated.h"

UENUM()
enum class EPCGExClusterClosestSearchMode : uint8
{
	Vtx  = 0 UMETA(DisplayName = "Closest vtx", ToolTip="Proximity to node position"),
	Edge = 1 UMETA(DisplayName = "Closest edge", ToolTip="Proximity to edge, then endpoint"),
};

UENUM()
enum class EPCGExClusterElement : uint8
{
	Vtx  = 0 UMETA(DisplayName = "Point", Tooltip="Value is fetched from the point being evaluated.", ActionIcon="Vtx"),
	Edge = 1 UMETA(DisplayName = "Edge", Tooltip="Value is fetched from the edge connecting to the point being evaluated.", ActionIcon="Edges"),
};

UENUM()
enum class EPCGExAdjacencyDirectionOrigin : uint8
{
	FromNode     = 0 UMETA(DisplayName = "From Node to Neighbor", Tooltip="..."),
	FromNeighbor = 1 UMETA(DisplayName = "From Neighbor to Node", Tooltip="..."),
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExNodeSelectionDetails
{
	GENERATED_BODY()

	FPCGExNodeSelectionDetails()
	{
	}

	explicit FPCGExNodeSelectionDetails(const double InMaxDistance)
		: MaxDistance(InMaxDistance)
	{
	}

	~FPCGExNodeSelectionDetails()
	{
	}

	/** Drives how the seed & goal points are selected within each cluster. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExClusterClosestSearchMode PickingMethod = EPCGExClusterClosestSearchMode::Edge;

	/** Max distance at which a node can be selected. Use <= 0 to ignore distance check. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=-1))
	double MaxDistance = -1;

	// TODO : Support local attribute

	FORCEINLINE bool WithinDistance(const FVector& NodePosition, const FVector& TargetPosition) const
	{
		if (MaxDistance <= 0)
		{
			return true;
		}
		return FVector::Distance(NodePosition, TargetPosition) < MaxDistance;
	}

	FORCEINLINE bool WithinDistanceSquared(const double SqDist) const
	{
		if (MaxDistance <= 0)
		{
			return true;
		}
		return SqDist < FMath::Square(MaxDistance);
	}
};

namespace PCGExClusters
{
	namespace States
	{
		PCGEX_CTX_STATE(State_ReadyForNextEdges)
	}

	/** Edge count above which cluster adjacency is built with the multi-pass parallel
	 * path instead of a single sequential loop. Below this, the extra passes cost more
	 * than they save, and sequential builds keep many-small-clusters batches free of
	 * task contention. The generic loop-dispatch cutoff is PCGExMT::DefaultParallelThreshold. */
	inline constexpr int32 ParallelClusterBuildThreshold = 4096;

	namespace Labels
	{
		const FName SourceEdgesLabel = TEXT("Edges");
		const FName OutputEdgesLabel = TEXT("Edges");

		const FName SourceVerticesLabel = TEXT("Vtx");
		const FName OutputVerticesLabel = TEXT("Vtx");

		const FName OutputKeptEdgesLabel = TEXT("Kept Edges");
		const FName OutputRemovedEdgesLabel = TEXT("Removed Edges");

		const FName OutputSitesLabel = TEXT("Sites");

		const FName SourceVtxFiltersLabel = FName("VtxFilters");
		const FName SourceEdgeFiltersLabel = FName("EdgeFilters");

		const FName SourcePackedClustersLabel = TEXT("Packed Clusters");
		const FName OutputPackedClustersLabel = TEXT("Packed Clusters");

		const FName SourceEdgeSortingRules = TEXT("Direction Sorting");

		const FName Attr_PCGExEdgeIdx = FName(PCGExCommon::PCGExPrefix + TEXT("EData"));
		const FName Attr_PCGExVtxIdx = FName(PCGExCommon::PCGExPrefix + TEXT("VData"));

		const FName Tag_PCGExCluster = FName(PCGExCommon::PCGExPrefix + TEXT("Cluster"));
		const FString TagStr_PCGExCluster = Tag_PCGExCluster.ToString();

		const FName Tag_PCGExVtx = FName(PCGExCommon::PCGExPrefix + TEXT("Vtx"));
		const FString TagStr_PCGExVtx = Tag_PCGExVtx.ToString();
		const FName Tag_PCGExEdges = FName(PCGExCommon::PCGExPrefix + TEXT("Edges"));
		const FString TagStr_PCGExEdges = Tag_PCGExEdges.ToString();


		const TSet<FName> ProtectedClusterAttributes = {Attr_PCGExEdgeIdx, Attr_PCGExVtxIdx};

		// TODO : Move at the right place

		const FName SourceProbesLabel = TEXT("Probes");
		const FName OutputProbeLabel = TEXT("Probe");

		const FName SourceFilterGenerators = TEXT("Generator Filters");
		const FName SourceFilterConnectables = TEXT("Connectable Filters");

		const FName Tag_PackedClusterEdgeCount_LEGACY = FName(PCGExCommon::PCGExPrefix + TEXT("PackedClusterEdgeCount"));
		const FName Tag_PackedClusterEdgeCount = FName(TEXT("@Data.") + PCGExCommon::PCGExPrefix + TEXT("PackedClusterEdgeCount"));

		const FName SourceGoalsLabel = TEXT("Goals");
		const FName SourcePlotsLabel = TEXT("Plots");
	}
}
