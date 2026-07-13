// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Core/PCGExPathfinding.h"
#include "Core/PCGExPointsProcessor.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExNavmesh.generated.h"

UENUM()
enum class EPCGExPathfindingNavmeshMode : uint8
{
	Regular      = 0 UMETA(DisplayName = "Regular", ToolTip="Regular pathfinding"),
	Hierarchical = 1 UMETA(DisplayName = "HIerarchical", ToolTip="Cell-based pathfinding"),
};

struct FPCGExNavmeshContext : FPCGExPointsProcessorContext
{
	FNavAgentProperties NavAgentProperties;

	bool bRequireNavigableEndLocation = true;
	EPCGExPathfindingNavmeshMode PathfindingMode;
	double FuseDistance = 10;
};

namespace PCGExNavmesh
{
	struct PCGEXELEMENTSPATHFINDINGNAVMESH_API FNavmeshQuery
	{
		PCGExPathfinding::FSeedGoalPair SeedGoalPair;
		TArray<FVector> Positions;
		PCGExPaths::FPathMetrics SeedGoalMetrics; // Metrics that go from seed to goal

		FNavmeshQuery() = default;
		explicit FNavmeshQuery(const PCGExPathfinding::FSeedGoalPair& InSeedGoalPair);

		bool IsValid() const
		{
			return !Positions.IsEmpty();
		}

		void FindPath(FPCGExNavmeshContext* InContext);
		void CopyPositions(const TPCGValueRange<FTransform>& InRange, int32& OutStartIndex, const bool bAddSeed, const bool bAddGoal);
	};
}
