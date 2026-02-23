// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchingCommon.generated.h"

struct FPCGPinProperties;
struct FPCGExMatchingDetails;

UENUM()
enum class EPCGExMapMatchMode : uint8
{
	Disabled = 0 UMETA(DisplayName = "Disabled", ToolTip="Disabled"),
	All      = 1 UMETA(DisplayName = "All", ToolTip="All tests must pass to consider a match successful"),
	Any      = 2 UMETA(DisplayName = "Any", ToolTip="Any single test must pass must to consider a match successful"),
};

UENUM()
enum class EPCGExMatchStrictness : uint8
{
	Required = 0 UMETA(DisplayName = "Required", ToolTip="Required check. If it fails, the match will be a fail."),
	Any      = 1 UMETA(DisplayName = "Optional", ToolTip="Optional check. If it fails but other pass, it's still a success."),
};

UENUM()
enum class EPCGExClusterComponentTagMatchMode : uint8
{
	Vtx       = 0 UMETA(DisplayName = "Vtx", ToolTip="Only match vtx (most efficient check)"),
	Edges     = 1 UMETA(DisplayName = "Edges", ToolTip="Only match edges"),
	Any       = 2 UMETA(DisplayName = "Any", ToolTip="Match either vtx or edges"),
	Both      = 3 UMETA(DisplayName = "Vtx and Edges", ToolTip="Match no vtx and edges"),
	Separated = 4 UMETA(DisplayName = "Separate", ToolTip="Uses two separate set of match handlers -- the default pin will be used on Vtx, the extra one for Edges."),
};

UENUM()
enum class EPCGExMatchingDetailsUsage : uint8
{
	Default  = 0,
	Cluster  = 1,
	Sampling = 2,
	Filter   = 3,
};

namespace PCGExMatching::Labels
{
	const FName OutputMatchRuleLabel = TEXT("Match Rule");
	const FName SourceMatchRulesLabel = TEXT("Match Rules");
	const FName SourceMatchRulesEdgesLabel = TEXT("Match Rules (Edges)");
	const FName OutputUnmatchedLabel = TEXT("Unmatched");
	const FName OutputUnmatchedVtxLabel = TEXT("Unmatched Vtx");
	const FName OutputUnmatchedEdgesLabel = TEXT("Unmatched Edges");
}
