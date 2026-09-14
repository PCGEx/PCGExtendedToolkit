// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCellSeedOwnership.h"

#include "PCGModule.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

namespace PCGExCells
{
	bool FSeedOwnershipHandler::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSeedsFacade)
	{
		bInitialized = false;

		if (!InSeedsFacade)
		{
			return false;
		}

		// Cache seed transforms for Closest mode
		SeedTransforms = InSeedsFacade->GetIn()->GetConstTransformValueRange();

		if (Method == EPCGExCellSeedOwnership::BestCandidate)
		{
			Sorter = MakeShared<PCGExSorting::FSorter>(InContext, InSeedsFacade.ToSharedRef(), PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules));
			Sorter->SortDirection = SortDirection;

			if (!Sorter->Init(InContext))
			{
				PCGEX_LOG_INVALID_INPUT(InContext, FTEXT("Missing or invalid sort rules for BestCandidate seed ownership"))
				return false;
			}

			// Build cache for faster comparisons
			SortCache = Sorter->BuildCache(InSeedsFacade->GetNum());
		}

		bInitialized = true;
		return true;
	}

	int32 FSeedOwnershipHandler::PickWinner(const TArray<int32>& Candidates, const FVector& CellCentroid) const
	{
		if (Candidates.IsEmpty())
		{
			return INDEX_NONE;
		}

		if (Candidates.Num() == 1)
		{
			return Candidates[0];
		}

		switch (Method)
		{
		case EPCGExCellSeedOwnership::SeedOrder:
		{
			// Lowest seed index wins, whatever order the candidates arrive in.
			int32 BestIdx = Candidates[0];
			for (int32 i = 1; i < Candidates.Num(); ++i)
			{
				BestIdx = FMath::Min(BestIdx, Candidates[i]);
			}
			return BestIdx;
		}

		case EPCGExCellSeedOwnership::Closest:
		{
			// Use full 3D world distance
			int32 BestIdx = Candidates[0];
			double BestDistSq = FVector::DistSquared(SeedTransforms[BestIdx].GetLocation(), CellCentroid);

			for (int32 i = 1; i < Candidates.Num(); ++i)
			{
				const int32 CandidateIdx = Candidates[i];
				const double DistSq = FVector::DistSquared(SeedTransforms[CandidateIdx].GetLocation(), CellCentroid);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestIdx = CandidateIdx;
				}
			}
			return BestIdx;
		}

		case EPCGExCellSeedOwnership::ClosestProjected:
		{
			// Use 2D distance (XY plane) to match the projected cell containment check
			int32 BestIdx = Candidates[0];
			double BestDistSq = FVector::DistSquaredXY(SeedTransforms[BestIdx].GetLocation(), CellCentroid);

			for (int32 i = 1; i < Candidates.Num(); ++i)
			{
				const int32 CandidateIdx = Candidates[i];
				const double DistSq = FVector::DistSquaredXY(SeedTransforms[CandidateIdx].GetLocation(), CellCentroid);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestIdx = CandidateIdx;
				}
			}
			return BestIdx;
		}

		case EPCGExCellSeedOwnership::BestCandidate:
		{
			if (!SortCache)
			{
				return Candidates[0];
			}

			int32 BestIdx = Candidates[0];
			for (int32 i = 1; i < Candidates.Num(); ++i)
			{
				const int32 CandidateIdx = Candidates[i];
				if (SortCache->Compare(CandidateIdx, BestIdx))
				{
					BestIdx = CandidateIdx;
				}
			}
			return BestIdx;
		}

		default:
			return Candidates[0];
		}
	}
}
