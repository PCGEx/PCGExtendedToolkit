// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

namespace PCGExProbing
{
	// Direction and GH are only filled when Distance (squared) is within some probe's GetSearchRadius:
	// a probe must test Distance against its own radius before reading either.
	struct PCGEXELEMENTSPROBING_API FCandidate
	{
		int32 PointIndex;
		FVector Direction;
		double Distance;
		uint64 GH;

		FCandidate(const int32 InIndex, const FVector& InDirection, const double InDistance, const uint64 InGH)
			: PointIndex(InIndex)
			  , Direction(InDirection)
			  , Distance(InDistance)
			  , GH(InGH)
		{
		}
	};

	struct PCGEXELEMENTSPROBING_API FBestCandidate
	{
		int32 BestIndex = -1;
		double BestPrimaryValue = 0;
		double BestSecondaryValue = 0;

		FBestCandidate()
		{
		}
	};
}
