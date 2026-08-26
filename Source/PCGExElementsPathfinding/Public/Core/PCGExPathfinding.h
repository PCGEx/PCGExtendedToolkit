// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExPointElements.h"
#include "PCGExPathfinding.generated.h"

namespace PCGEx
{
	class FScoredQueue;
	class FHashLookup;
}

namespace PCGExMT
{
	class FTaskManager;
}

class UPCGExGoalPicker;

namespace PCGExData
{
	class FFacade;
}


struct FPCGExNodeSelectionDetails;

namespace PCGExClusters
{
	class FCluster;
	struct FNode;
}

namespace PCGExHeuristics
{
	class FLocalFeedbackHandler;
}

class FPCGExSearchOperation;

namespace PCGExHeuristics
{
	class FHandler;
}

UENUM()
enum class EPCGExPathComposition : uint8
{
	Vtx         = 0 UMETA(DisplayName = "Vtx", Tooltip="..."),
	Edges       = 1 UMETA(DisplayName = "Edge", Tooltip="..."),
	VtxAndEdges = 2 UMETA(Hidden, DisplayName = "Vtx & Edges", Tooltip="..."),
};

UENUM()
enum class EPCGExPathfindingOutputMode : uint8
{
	Paths   = 0 UMETA(DisplayName = "Paths", Tooltip="Output one path data per resolved query (default behavior)."),
	Visited = 1 UMETA(DisplayName = "Visited", Tooltip="Do not output paths. Instead, forward Vtx & Edges and write, per element, how many output paths visit it."),
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="pathfinding/common-settings/path-statistics"))
struct PCGEXELEMENTSPATHFINDING_API FPCGExPathStatistics
{
	GENERATED_BODY()

	FPCGExPathStatistics()
	{
	}

	virtual ~FPCGExPathStatistics() = default;

	/** Write, per vtx, the number of output paths that visit it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePointUseCount = true;

	/** Name of the 'int32' attribute to write the per-vtx visited count to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Visited (Vtx)", PCG_Overridable, EditCondition="bWritePointUseCount"))
	FName PointUseCountAttributeName = FName("Visited");
	
	/** Write, per edge, the number of output paths that visit it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteEdgeUseCount = true;

	/** Name of the 'int32' attribute to write the per-edge visited count to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Visited (Edges)", PCG_Overridable, EditCondition="bWriteEdgeUseCount"))
	FName EdgeUseCountAttributeName = FName("Visited");
	
	/** If disabled, increment existing attributes otherwise reset to 0. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
    bool bResetValues = true;
};

namespace PCGExPathfinding
{
	class FPathQuery;
	class FPlotQuery;

	namespace Labels
	{
		const FName SourceOverridesGoalPicker = TEXT("Overrides : Goal Picker");
		const FName SourceOverridesSearch = TEXT("Overrides : Search");
	}

	enum class EQueryPickResolution : uint8
	{
		None = 0,
		Success,
		UnresolvedSeed,
		UnresolvedGoal,
		UnresolvedPicks,
		SameSeedAndGoal,
	};

	enum class EPathfindingResolution : uint8
	{
		None = 0,
		Success,
		Fail
	};

	struct PCGEXELEMENTSPATHFINDING_API FNodePick
	{
		explicit FNodePick(const PCGExData::FConstPoint& InSourcePointRef)
			: Point(InSourcePointRef)
		{
		}

		PCGExData::FConstPoint Point;
		const PCGExClusters::FNode* Node = nullptr;

		bool IsValid() const
		{
			return Node != nullptr;
		};
		bool ResolveNode(const TSharedRef<PCGExClusters::FCluster>& InCluster, const FPCGExNodeSelectionDetails& SelectionDetails);

		operator PCGExData::FConstPoint() const
		{
			return Point;
		}
	};

	struct PCGEXELEMENTSPATHFINDING_API FSeedGoalPair
	{
		int32 Seed = -1;
		FVector SeedPosition = FVector::ZeroVector;
		int32 Goal = -1;
		FVector GoalPosition = FVector::ZeroVector;

		FSeedGoalPair() = default;

		FSeedGoalPair(const int32 InSeed, const FVector& InSeedPosition, const int32 InGoal, const FVector& InGoalPosition)
			: Seed(InSeed)
			  , SeedPosition(InSeedPosition)
			  , Goal(InGoal)
			  , GoalPosition(InGoalPosition)
		{
		}

		FSeedGoalPair(const PCGExData::FConstPoint& InSeed, const PCGExData::FConstPoint& InGoal)
			: Seed(InSeed.Index)
			  , SeedPosition(InSeed.GetLocation())
			  , Goal(InGoal.Index)
			  , GoalPosition(InGoal.GetLocation())
		{
		}

		bool IsValid() const
		{
			return Seed != -1 && Goal != -1;
		}
	};

	PCGEXELEMENTSPATHFINDING_API
	void ProcessGoals(const TSharedPtr<PCGExData::FFacade>& InSeedDataFacade, const UPCGExGoalPicker* GoalPicker, TFunction<void(int32, int32)>&& GoalFunc);

	// Atomically increments the per-element visited counts for a single resolved path query.
	// Each entry maps through the cluster to a vtx/edge point-data index (same indices a path
	// would output). Buffers may be null when the corresponding output is disabled.
	PCGEXELEMENTSPATHFINDING_API
	void MarkQueryVisited(const PCGExClusters::FCluster& Cluster, const FPathQuery& Query, int32* VtxVisitedCounts, int32* EdgeVisitedCounts);

	// Atomically increments the per-element visited counts for a whole plot (one output path).
	// Distinct elements across the plot's successful sub-queries are counted once, so a junction
	// shared by consecutive sub-paths contributes +1, not +2. Buffers may be null.
	PCGEXELEMENTSPATHFINDING_API
	void MarkPlotVisited(const PCGExClusters::FCluster& Cluster, const FPlotQuery& Plot, int32* VtxVisitedCounts, int32* EdgeVisitedCounts);
}
