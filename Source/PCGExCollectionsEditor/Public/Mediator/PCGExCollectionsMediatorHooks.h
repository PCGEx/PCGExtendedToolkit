// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * Mediator value hooks for the property types this module owns (registered with the Properties hook registry):
 *   CollectionEntry -> { "collection": path, "entryId": N }, structural "lockCollection"
 *   Range           -> output-space [min, max] (the output-type default), structural "min" / "max" bounds
 */
namespace PCGExCollectionsMediator
{
	void RegisterHooks();
	void UnregisterHooks();
}
