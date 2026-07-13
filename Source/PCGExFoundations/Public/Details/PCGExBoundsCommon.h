// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"

#include "PCGExBoundsCommon.generated.h"

UENUM()
enum class EPCGExPointOnBoundsOutputMode : uint8
{
	Merged     = 0 UMETA(DisplayName = "Merged Points", Tooltip="..."),
	Individual = 1 UMETA(DisplayName = "Per-point dataset", Tooltip="..."),
};
