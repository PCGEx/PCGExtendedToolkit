// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * Mediator bindings for the hosts this module owns: the Property Schema asset and the Tuple node. Distribute
 * Tuple lives in Collections and binds from there.
 */
namespace PCGExPropertyMediatorBindings
{
	void Register();
	void Unregister();
}
