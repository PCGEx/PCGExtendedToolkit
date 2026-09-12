// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FPCGExAssetSaveTargetDetails;
struct FPCGExRoamingAssetCollectionDetails;
struct FPCGExContext;
class UPCGExAssetCollection;
class UPCGParamData;

/** Writing a generated collection to a real asset. Editor-only in effect; non-editor builds no-op. */
namespace PCGExCollectionSave
{
	/** EntryId from the source asset path, so external references (FPCGExVariantSource::SourceEntryId) survive
	 *  a regeneration; never 0. Occurrence separates rows repeating one path. CRC over the path STRING, never
	 *  GetTypeHash -- FSoftObjectPath's hash folds session-local FName indices. */
	PCGEXCOLLECTIONS_API int32 MakeStableEntryId(const FSoftObjectPath& InPath, int32 InOccurrence);

	/** Stamp path-derived ids in iteration order; an entry with no staged path keeps 0 for SyncEntryIds.
	 *  Deduplicated within the collection, which is all EntryId uniqueness requires. */
	PCGEXCOLLECTIONS_API void StampStableEntryIds(UPCGExAssetCollection* InCollection);

	/** Build an Omni collection from InAttributeSet into the target asset and save it. Reuses an occupant of
	 *  the same class so its CollectionGUID survives. Null on any refusal, all of which are logged. */
	PCGEXCOLLECTIONS_API UPCGExAssetCollection* SaveOmniFromAttributeSet(
		const FPCGExAssetSaveTargetDetails& InTarget,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		const FPCGExRoamingAssetCollectionDetails& InDetails);
}
