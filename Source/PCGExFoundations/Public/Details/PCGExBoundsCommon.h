// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"

#include "PCGExBoundsCommon.generated.h"

UENUM()
enum class EPCGExPointOnBoundsOutputMode : uint8
{
	Merged     = 0 UMETA(DisplayName = "Merged Points", Tooltip="Output one dataset holding a single point per input, in input order. Attributes are merged across inputs, and same-name attributes of different types collapse onto the first type seen."),
	Individual = 1 UMETA(DisplayName = "Per-point dataset", Tooltip="Output one single-point dataset per input, keeping that input's own attributes and tags."),
};
