// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class UPCGExAssetCollection;
class UPackage;

/** Utility functions for collection editing. Operate on any UPCGExAssetCollection. */
namespace PCGExCollectionEditorUtils
{
	/** Add Content Browser selection to this collection. */
	PCGEXCOLLECTIONSEDITOR_API void AddBrowserSelection(UPCGExAssetCollection* InCollection);

#pragma region Tools

	/** Sort collection by weights in ascending order. */
	PCGEXCOLLECTIONSEDITOR_API void SortByWeightAscending(UPCGExAssetCollection* InCollection);

	/** Sort collection by weights in descending order. */
	PCGEXCOLLECTIONSEDITOR_API void SortByWeightDescending(UPCGExAssetCollection* InCollection);

	/** Set weights to match entry index order. */
	PCGEXCOLLECTIONSEDITOR_API void SetWeightIndex(UPCGExAssetCollection* InCollection);

	/** Add 1 to all weights so it's easier to weight down some assets */
	PCGEXCOLLECTIONSEDITOR_API void PadWeight(UPCGExAssetCollection* InCollection);

	/** Multiplies all weights by 2 */
	PCGEXCOLLECTIONSEDITOR_API void MultWeight(UPCGExAssetCollection* InCollection, int32 Mult);

	/** Reset all weights to 100 */
	PCGEXCOLLECTIONSEDITOR_API void WeightOne(UPCGExAssetCollection* InCollection);

	/** Assign random weights to items */
	PCGEXCOLLECTIONSEDITOR_API void WeightRandom(UPCGExAssetCollection* InCollection);

	/** Normalize weight sum to 100 */
	PCGEXCOLLECTIONSEDITOR_API void NormalizedWeightToSum(UPCGExAssetCollection* InCollection);

#pragma endregion
}
