// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPCGExMediatorDomain;

/**
 * Mediator bindings for the hosts this module owns: every asset collection (bound on the base class, so typed,
 * Variant and Omni hosts all resolve) and the Distribute Tuple node.
 */
namespace PCGExCollectionsMediatorBindings
{
	void Register(FPCGExMediatorDomain& Domain);
}
