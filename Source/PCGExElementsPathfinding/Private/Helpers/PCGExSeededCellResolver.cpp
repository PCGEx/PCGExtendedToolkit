// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExSeededCellResolver.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExCellSeedOwnership.h"
#include "Math/PCGExProjectionDetails.h"

namespace PCGExCells
{
	FSeededCellResolver::FSeededCellResolver(const FConfig& InConfig)
		: Cluster(InConfig.Cluster)
		  , SeedsDataFacade(InConfig.SeedsDataFacade)
		  , SeedPicking(InConfig.SeedPicking)
		  , SeedOwnership(InConfig.SeedOwnership)
		  , SeedGrowth(InConfig.SeedGrowth)
		  , SeedMerge(InConfig.SeedMerge)
		  , bSoloClusterWorkload(InConfig.bSoloClusterWorkload)
	{
		check(Cluster && SeedsDataFacade && InConfig.Projection && SeedPicking && SeedOwnership && SeedGrowth && SeedMerge);

		const int32 NumSeeds = SeedsDataFacade->Source->GetNum();

		// Create projected seed set (lazy projection with AABB)
		Seeds = MakeShared<PCGExClusters::FProjectedPointSet>(InConfig.Context, SeedsDataFacade.ToSharedRef(), *InConfig.Projection);
		Seeds->EnsureProjected(); // Project once upfront before any loops

		SeedInBounds.Init(true, NumSeeds);
		if (SeedPicking->bWithinClusterBounds)
		{
			TConstPCGValueRange<FTransform> SeedTransforms = SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			for (int32 i = 0; i < NumSeeds; i++)
			{
				SeedInBounds[i] = SeedPicking->WithinBounds(Cluster->Bounds, SeedTransforms[i].GetLocation());
			}
		}

		MaxPlaneDistSq = SeedPicking->MaxDistance > 0 ? FMath::Square(SeedPicking->MaxDistance) : -1.0;
	}

	void FSeededCellResolver::SetCells(
		TArray<TSharedPtr<PCGExClusters::FCell>>&& InCells,
		TArray<TSharedPtr<PCGExClusters::FCell>>&& InFailedCells,
		const TSharedRef<PCGExClusters::FCellConstraints>& InConstraints,
		const TSharedRef<PCGExClusters::FPlanarFaceEnumerator>& InEnumerator)
	{
		Constraints = InConstraints;
		Enumerator = InEnumerator;

		// Growth expands through adjacency; merging splits groups by it.
		if (SeedGrowth->HasPotentialGrowth() || SeedMerge->IsEnabled())
		{
			CellAdjacencyMap = &Enumerator->GetOrBuildAdjacencyMap(Enumerator->GetWrapperFaceIndex());
		}

		// Combine valid and failed internal cells for consumption tracking
		// (seeds inside ANY internal cell polygon are "consumed" - can't claim wrapper)
		AllCellsIncludingFailed = InCells;
		AllCellsIncludingFailed.Append(InFailedCells);

		EnumeratedCells = MoveTemp(InCells);

		// LocalTangent: arbitrate seed claims up front -- a seed sandwiched between STACKED parallel
		// cells projects inside both, and only the nearest plane may own it.
		if (Enumerator->IsLocalTangent() && !EnumeratedCells.IsEmpty())
		{
			const int32 NumSeeds = SeedInBounds.Num();
			TConstPCGValueRange<FTransform> SeedTransforms = SeedsDataFacade->GetIn()->GetConstTransformValueRange();

			SeedBestFace.Init(INDEX_NONE, NumSeeds);
			FailedCellsOnly = MoveTemp(InFailedCells);

			// Parallel when the pair count is heavy, or earlier when this is the only cluster in flight
			// (nested parallelism then competes with nothing -- bSoloClusterWorkload).
			const int64 PairOps = static_cast<int64>(NumSeeds) * EnumeratedCells.Num();
			const int32 NestedParallelThreshold = (bSoloClusterWorkload && PairOps > 4096) || PairOps > 65536 ? 32 : MAX_int32;

			PCGExMT::ParallelOrSequential(
				NumSeeds, [&](const int32 SeedIdx)
				{
					if (!SeedInBounds[SeedIdx])
					{
						return;
					}

					const FVector SeedPos = SeedTransforms[SeedIdx].GetLocation();
					const FVector2D SeedProjected = Seeds->GetProjected(SeedIdx);
					double BestDistSq = TNumericLimits<double>::Max();
					for (const TSharedPtr<PCGExClusters::FCell>& Cell : EnumeratedCells)
					{
						double DistSq = 0;
						if (!Cell || !Cell->ContainsPoint(SeedProjected, SeedPos, MaxPlaneDistSq, &DistSq))
						{
							continue;
						}
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							SeedBestFace[SeedIdx] = Cell->FaceIndex;
						}
					}
				}, NestedParallelThreshold);

			// Inverse map, so ProcessRange resolves each cell's claimants with one lookup.
			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (SeedBestFace[SeedIdx] != INDEX_NONE)
				{
					SeedFaceClaims.FindOrAdd(SeedBestFace[SeedIdx]).Add(SeedIdx);
				}
			}
		}
	}

	void FSeededCellResolver::PrepareScopes(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedValidCells = MakeShared<PCGExMT::TScopedArray<TSharedPtr<PCGExClusters::FCell>>>(Loops);
	}

	void FSeededCellResolver::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const int32 NumSeeds = Seeds->Num();
		const bool bNeedsAllCandidates = SeedOwnership->NeedsAllCandidates();

		// LocalTangent cells re-project each seed into their own face frame, optionally gated on distance
		// to the face plane (Seed Picking's MaxDistance); planar cells test in the shared projection.
		TConstPCGValueRange<FTransform> SeedTransforms = SeedsDataFacade->GetIn()->GetConstTransformValueRange();

		TArray<TSharedPtr<PCGExClusters::FCell>>& CellsContainer = ScopedValidCells->Get_Ref(Scope);
		CellsContainer.Reserve(Scope.Count);

		TArray<int32> CandidateSeeds; // Reused per cell
		CandidateSeeds.Reserve(8);

		PCGEX_SCOPE_LOOP(CellIndex)
		{
			const TSharedPtr<PCGExClusters::FCell>& Cell = EnumeratedCells[CellIndex];
			if (!Cell || Cell->Polygon.IsEmpty())
			{
				continue;
			}

			CandidateSeeds.Reset();

			if (!SeedBestFace.IsEmpty())
			{
				// Arbitrated (LocalTangent): the inverse map already holds this cell's claimants.
				if (const TArray<int32>* Claims = SeedFaceClaims.Find(Cell->FaceIndex))
				{
					CandidateSeeds = *Claims;
				}
			}
			else
			{
				for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
				{
					if (!SeedInBounds[SeedIdx])
					{
						continue;
					}

					if (Cell->ContainsPoint(Seeds->GetProjected(SeedIdx), SeedTransforms[SeedIdx].GetLocation(), MaxPlaneDistSq))
					{
						CandidateSeeds.Add(SeedIdx);

						// For SeedOrder mode, first match wins - break early
						if (!bNeedsAllCandidates)
						{
							break;
						}
					}
				}
			}

			// Only output cells that contain at least one seed
			if (!CandidateSeeds.IsEmpty())
			{
				const int32 WinnerSeedIndex = SeedOwnership->PickWinner(CandidateSeeds, Cell->Data.Centroid);
				Cell->CustomIndex = WinnerSeedIndex;
				CellsContainer.Add(Cell);
			}
		}
	}

	void FSeededCellResolver::Finalize(TArray<TSharedPtr<PCGExClusters::FCell>>& OutCells)
	{
		// No scopes when the range loop never ran: there was no cell to claim.
		if (ScopedValidCells)
		{
			ScopedValidCells->Collapse(OutCells);
			ScopedValidCells.Reset();
		}

		if (!CellAdjacencyMap)
		{
			return;
		}

		auto PickOwner = [&](const TArray<int32>& SeedIndices, const FVector& Centroid)
		{
			return SeedOwnership->PickWinner(SeedIndices, Centroid);
		};

		// Grow seed claims through adjacency; only constraint-passing cells (EnumeratedCells) can be claimed.
		PCGExClusters::GrowSeedClaims(OutCells, EnumeratedCells, *CellAdjacencyMap, *SeedGrowth, PickOwner);

		// Merge adjacent cells, grouped by seed key value or by owning seed (the latter only differs with growth).
		if (SeedMerge->IsEnabled() || (SeedGrowth->bMergeAdjacentCells && SeedGrowth->HasPotentialGrowth()))
		{
			PCGExClusters::MergeCellGroups(
				OutCells, Constraints.ToSharedRef(), Cluster.Get(), *CellAdjacencyMap,
				[&](const PCGExClusters::FCell& Cell)
				{
					return SeedMerge->GetKey(Cell.CustomIndex);
				},
				PickOwner);
		}
	}

	int32 FSeededCellResolver::PickWrapperSeed(const TArray<TSharedPtr<PCGExClusters::FCell>>& InClaimedCells, const FVector& InWrapperCentroid) const
	{
		// Collect consumed seed indices (seeds that matched a valid internal cell)
		TSet<int32> ConsumedSeeds;
		for (const TSharedPtr<PCGExClusters::FCell>& Cell : InClaimedCells)
		{
			if (Cell)
			{
				ConsumedSeeds.Add(Cell->CustomIndex);
				ConsumedSeeds.Append(Cell->ContributorIndices);
			}
		}

		// Also mark seeds inside failed cells as consumed. Arbitration (when it ran) already proves
		// membership in a VALID cell, so only the failed cells still need testing.
		const int32 NumSeeds = Seeds->Num();
		TConstPCGValueRange<FTransform> SeedTransforms = SeedsDataFacade->GetIn()->GetConstTransformValueRange();

		const bool bArbitrated = !SeedBestFace.IsEmpty();
		const TArray<TSharedPtr<PCGExClusters::FCell>>& ConsumptionCells = bArbitrated ? FailedCellsOnly : AllCellsIncludingFailed;

		for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
		{
			if (!SeedInBounds[SeedIdx] || ConsumedSeeds.Contains(SeedIdx))
			{
				continue;
			}

			if (bArbitrated && SeedBestFace[SeedIdx] != INDEX_NONE)
			{
				ConsumedSeeds.Add(SeedIdx);
				continue;
			}

			const FVector2D& SeedPoint = Seeds->GetProjected(SeedIdx);

			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ConsumptionCells)
			{
				if (Cell && Cell->ContainsPoint(SeedPoint, SeedTransforms[SeedIdx].GetLocation(), MaxPlaneDistSq))
				{
					ConsumedSeeds.Add(SeedIdx);
					break;
				}
			}
		}

		// Find best exterior seed within picking distance
		TArray<int32> CandidateSeeds;
		CandidateSeeds.Reserve(NumSeeds);

		for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
		{
			if (!SeedInBounds[SeedIdx] || ConsumedSeeds.Contains(SeedIdx))
			{
				continue;
			}

			if (SeedPicking->WithinDistanceOfEdges(*Cluster, SeedTransforms[SeedIdx].GetLocation()))
			{
				CandidateSeeds.Add(SeedIdx);
			}
		}

		return SeedOwnership->PickWinner(CandidateSeeds, InWrapperCentroid);
	}
}
