// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

struct FPCGExContext;
struct FPCGExNodeSelectionDetails;
struct FPCGExCellGrowthDetails;
struct FPCGExCellSeedMergeDetails;
struct FPCGExGeo2DProjectionDetails;

namespace PCGExMT
{
	struct FScope;

	template <typename T>
	class TScopedArray;
}

namespace PCGExData
{
	class FFacade;
}

namespace PCGExClusters
{
	class FCluster;
	class FCell;
	class FCellConstraints;
	class FPlanarFaceEnumerator;
	class FProjectedPointSet;
}

namespace PCGExCells
{
	class FSeedOwnershipHandler;

	/**
	 * Seed-to-cell resolution shared by the seeded cell finders: bounds gate, LocalTangent arbitration, per-cell
	 * candidate scan and ownership, growth, merge, and the wrapper claim.
	 * The owning processor enumerates the cells, forwards its range hooks here and emits the result.
	 */
	class PCGEXELEMENTSPATHFINDING_API FSeededCellResolver
	{
	public:
		/** Everything pointed at must outlive the resolver. */
		struct FConfig
		{
			FPCGExContext* Context = nullptr;
			TSharedPtr<PCGExClusters::FCluster> Cluster;
			TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
			const FPCGExGeo2DProjectionDetails* Projection = nullptr;
			const FPCGExNodeSelectionDetails* SeedPicking = nullptr;
			const FSeedOwnershipHandler* SeedOwnership = nullptr;
			const FPCGExCellGrowthDetails* SeedGrowth = nullptr;
			const FPCGExCellSeedMergeDetails* SeedMerge = nullptr;
			bool bSoloClusterWorkload = false;
		};

		/** Projects the seeds and resolves the per-seed cluster-bounds gate. */
		explicit FSeededCellResolver(const FConfig& InConfig);

		/**
		 * Takes the enumerated cells: InCells passed the constraints and can be claimed, InFailedCells only consume
		 * the seeds they hold. Fetches the adjacency map when growth or merge needs it; on LocalTangent, arbitrates
		 * each seed's claim.
		 */
		void SetCells(
			TArray<TSharedPtr<PCGExClusters::FCell>>&& InCells,
			TArray<TSharedPtr<PCGExClusters::FCell>>&& InFailedCells,
			const TSharedRef<PCGExClusters::FCellConstraints>& InConstraints,
			const TSharedRef<PCGExClusters::FPlanarFaceEnumerator>& InEnumerator);

		/** Number of claimable cells: the range to loop over. */
		FORCEINLINE int32 NumCells() const
		{
			return EnumeratedCells.Num();
		}

		void PrepareScopes(const TArray<PCGExMT::FScope>& Loops);

		/** Finds each cell's candidate seeds and picks its owner. Safe to run on concurrent scopes. */
		void ProcessRange(const PCGExMT::FScope& Scope);

		/** Appends the claimed cells to OutCells in cell order, then grows and merges the claims. */
		void Finalize(TArray<TSharedPtr<PCGExClusters::FCell>>& OutCells);

		/**
		 * The seed that claims the wrapper: one that no cell consumed, claimed or failed, and that passes Seed
		 * Picking's distance gate. INDEX_NONE when there is none.
		 */
		int32 PickWrapperSeed(const TArray<TSharedPtr<PCGExClusters::FCell>>& InClaimedCells, const FVector& InWrapperCentroid) const;

	private:
		TSharedPtr<PCGExClusters::FCluster> Cluster;
		TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
		const FPCGExNodeSelectionDetails* SeedPicking = nullptr;
		const FSeedOwnershipHandler* SeedOwnership = nullptr;
		const FPCGExCellGrowthDetails* SeedGrowth = nullptr;
		const FPCGExCellSeedMergeDetails* SeedMerge = nullptr;
		bool bSoloClusterWorkload = false;

		TSharedPtr<PCGExClusters::FProjectedPointSet> Seeds;

		/** Seed Picking's MaxDistance squared, as the cell-plane distance gate; < 0 when the gate is off. */
		double MaxPlaneDistSq = -1.0;

		/** Per-seed result of Seed Picking's cluster-bounds gate, precomputed once (all-true when the gate is off). */
		TBitArray<> SeedInBounds;

		TSharedPtr<PCGExClusters::FCellConstraints> Constraints;
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator;

		TArray<TSharedPtr<PCGExClusters::FCell>> EnumeratedCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> AllCellsIncludingFailed; // For checking seed consumption

		/** LocalTangent only: per-seed FaceIndex of its nearest-plane containing cell (INDEX_NONE = none).
		 *  STACKED parallel cells both contain a sandwiched seed's projection; this arbitrates the claim.
		 *  Empty on planar builds, where faces are disjoint in the shared 2D space. */
		TArray<int32> SeedBestFace;

		/** Inverse of SeedBestFace, built once after arbitration: FaceIndex -> its claiming seeds.
		 *  Turns ProcessRange's per-cell seed scan into a single lookup. */
		TMap<int32, TArray<int32>> SeedFaceClaims;

		/** LocalTangent only: the failed cells alone -- arbitration already proves membership in VALID
		 *  cells, so the wrapper consumption sweep only needs to test these. */
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCellsOnly;

		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<PCGExClusters::FCell>>> ScopedValidCells;

		/** Owned by the enumerator. */
		const TMap<int32, TSet<int32>>* CellAdjacencyMap = nullptr;
	};
}
