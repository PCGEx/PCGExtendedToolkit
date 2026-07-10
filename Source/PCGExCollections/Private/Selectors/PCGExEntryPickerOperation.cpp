// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExEntryPickerOperation.h"

#include "Data/PCGExData.h"
#include "Helpers/PCGExRandomHelpers.h"

bool FPCGExEntryPickerOperation::PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection)
{
	BindContext(InContext);
	PrimaryDataFacade = InDataFacade;
	Target = InTarget;
	OwningCollection = InOwningCollection;
	// Reject null and empty up-front so the hot path (Pick) can assume Target is valid + non-empty.
	// Empty categories should never reach here in practice (FCache::RegisterEntry only creates
	// categories on first valid entry), but the assertion belongs at the boundary.
	return Target != nullptr && !Target->IsEmpty();
}

int32 FPCGExEntryPickerOperation::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	// Fallback for selectors that don't override: veto+retry with salted seeds. The raw->local
	// linear find is acceptable here -- this is the degraded path for exclusion-unaware
	// (typically third-party) selectors; built-ins all override with exclusion-aware loops.
	constexpr int32 MaxVetoAttempts = 8;
	int32 PreviousRaw = -1;

	for (int32 Attempt = 0; Attempt < MaxVetoAttempts; ++Attempt)
	{
		const int32 Raw = Pick(PointIndex, Attempt == 0 ? Seed : PCGExRandomHelpers::GetSeed(Seed, Attempt), Scratch);
		if (Raw == -1)
		{
			return -1;
		}

		const int32 Local = Target->Indices.IndexOfByKey(Raw);
		if (Local != INDEX_NONE && InAvailability.IsAvailable(Local))
		{
			return Raw;
		}

		// A re-seeded retry returning the same vetoed pick means the selector is deterministic
		// for this point -- no amount of further attempts can escape. Treat as exhausted.
		if (Raw == PreviousRaw)
		{
			return -1;
		}
		PreviousRaw = Raw;
	}

	return -1;
}
