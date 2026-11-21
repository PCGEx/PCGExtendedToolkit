// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollection.h"
#include "Factories/PCGExOperation.h"

namespace PCGExData
{
	class FFacade;
}

struct FPCGExContext;

/**
 * Abstract hot-path operation for picking a sub-entry (variant) index within a MicroCache.
 *
 * Mirrors FPCGExEntryPickerOperation but targets FMicroCache* -- the per-entry data used for
 * material variant picking on mesh collections. For typical use, a single op is created per
 * factory and reused across entries; the op holds no per-target state.
 *
 * Concrete subclasses implement Pick() with a tight, branch-minimal body.
 */
class PCGEXCOLLECTIONS_API FPCGExMicroEntryPickerOperation : public FPCGExOperation
{
public:
	/**
	 * Bind the operation to a data facade. MicroCache targets are re-bound per-pick by the
	 * consumer (since each entry has its own MicroCache) so PrepareForData does NOT take a target.
	 * @return false if initialization fails.
	 */
	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade);

	/**
	 * Pick a variant index from the given MicroCache. Returns -1 for empty/invalid caches.
	 * Called per-point in parallel scopes -- must be thread-safe and free of mutation.
	 */
	virtual int32 Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const = 0;
};
