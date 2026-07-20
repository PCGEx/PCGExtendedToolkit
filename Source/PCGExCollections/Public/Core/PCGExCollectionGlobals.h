// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCollectionGlobals.generated.h"

/**
 * Base for per-type collection-level settings blocks ("type globals") -- the interchange
 * format of UPCGExAssetCollection::GetTypeGlobals. Entries query their host through it
 * instead of casting to a concrete collection class; hosts that cannot answer leave the
 * entry on its local-value fallbacks.
 *
 * Typed collections do NOT store these structs (existing UPROPERTYs stay, zero data
 * migration) -- they assemble the block on demand; only heterogeneous hosts store blocks.
 * Derived structs mirror the typed collection's members 1:1 (straight per-property copy).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExCollectionTypeGlobals
{
	GENERATED_BODY()

	FPCGExCollectionTypeGlobals() = default;
	virtual ~FPCGExCollectionTypeGlobals() = default;
};
