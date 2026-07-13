// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"

#include "PCGExSortingCommon.generated.h"

enum class EPCGPinStatus : uint8;
struct FPCGPinProperties;

UENUM()
enum class EPCGExSortDirection : uint8
{
	Ascending  = 0 UMETA(DisplayName = "Ascending", ToolTip = "Ascending", ActionIcon="Ascending"),
	Descending = 1 UMETA(DisplayName = "Descending", ToolTip = "Descending", ActionIcon="Descending")
};

namespace PCGExSorting::Labels
{
	const FName SourceSortingRules = TEXT("SortRules");
}
