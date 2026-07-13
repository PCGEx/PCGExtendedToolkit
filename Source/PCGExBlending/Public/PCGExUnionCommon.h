// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"

#include "PCGExUnionCommon.generated.h"

UENUM()
enum class EPCGExFuseMethod : uint8
{
	Voxel  = 0 UMETA(DisplayName = "Spatial Hash", Tooltip="Fast but blocky. Creates grid-looking approximation."),
	Octree = 1 UMETA(DisplayName = "Octree", Tooltip="Slow but precise. Respectful of the original topology. Requires stable insertion with large values."),
};

namespace PCGExGraphs::States
{
	PCGEX_CTX_STATE(State_PreparingUnion)
	PCGEX_CTX_STATE(State_ProcessingUnion)

	PCGEX_CTX_STATE(State_ProcessingPointEdgeIntersections)
	PCGEX_CTX_STATE(State_ProcessingEdgeEdgeIntersections)
}
