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
		int32 BestPointIndex = -1; // FCandidate::PointIndex of the best candidate, the last tie-break
		double BestPrimaryValue = 0;
		double BestSecondaryValue = 0;

		FBestCandidate()
		{
		}

		/** Keeps the better of Candidate and the current best: favored metric, then the other one, then lowest point index. */
		void ConsiderDirectional(const int32 CandidateIndex, const FCandidate& Candidate, const double Dot, const bool bFavorAlignment)
		{
			// Lower wins for both values, so alignment is stored negated.
			const double Primary = bFavorAlignment ? -Dot : Candidate.Distance;
			const double Secondary = bFavorAlignment ? Candidate.Distance : -Dot;

			if (BestIndex == -1
				|| Primary < BestPrimaryValue
				|| (Primary == BestPrimaryValue && (Secondary < BestSecondaryValue || (Secondary == BestSecondaryValue && Candidate.PointIndex < BestPointIndex))))
			{
				BestIndex = CandidateIndex;
				BestPointIndex = Candidate.PointIndex;
				BestPrimaryValue = Primary;
				BestSecondaryValue = Secondary;
			}
		}
	};
}
