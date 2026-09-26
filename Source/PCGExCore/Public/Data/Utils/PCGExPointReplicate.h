// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class UPCGBasePointData;

namespace PCGExData
{
	class FDataForwardHandler;
}

namespace PCGExPointReplicate
{
	struct FCopies
	{
		/** Copy k is placed with Transforms[k]; the copy count is Transforms.Num(). */
		TConstArrayView<FTransform> Transforms;

		/** How each copy's transform combines with its points (PCGExFitting::GetInheritStrategy). */
		int32 InheritStrategy = 3;

		/** Recompute seeds from the transformed locations. */
		bool bRefreshSeeds = false;
	};

	struct FForward
	{
		const PCGExData::FDataForwardHandler* Handler = nullptr;

		/** Forward-source row per copy; must match FCopies::Transforms.Num(). */
		TConstArrayView<int32> SourceIndices;
	};

	/**
	 * Fills Out with every copy of Source, copy k at points [k*N, (k+1)*N). Out must be freshly initialized from Source
	 * (EIOInit::New) so its metadata is parented to Source's: carried attributes are inherited through the parent keys,
	 * never copied. Blocking and parallel inside, so call it from a task. False when the copies exceed int32 points.
	 */
	PCGEXCORE_API bool Replicate(const UPCGBasePointData* Source, UPCGBasePointData* Out, const FCopies& Copies, const FForward* Forward = nullptr);
}
