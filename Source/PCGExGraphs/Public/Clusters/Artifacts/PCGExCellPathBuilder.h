// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

struct FPCGExCellArtifactsDetails;
struct FPCGExCellSeedMutationDetails;
struct FPCGExAttributeToTagDetails;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FFacade;
	class FPointIO;
	class FDataForwardHandler;
	struct FIOSortKey;
}

namespace PCGExClusters
{
	class FCluster;
	class FCell;

	/**
	 * Helper class for building path outputs from cells.
	 * Configure once per cluster processor, then call Build methods for each cell.
	 * Keeps PCGExCellDetails.h clean (settings structs only).
	 */
	class PCGEXGRAPHS_API FCellPathBuilder
	{
	public:
		FCellPathBuilder() = default;

		//~ Core references (required)
		TSharedPtr<FCluster> Cluster;
		TSharedPtr<PCGExMT::FTaskManager> TaskManager;
		const FPCGExCellArtifactsDetails* Artifacts = nullptr;

		//~ For non-seeded variants: paths stage by edges dataset first
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

		//~ For seeded variants
		int32 BatchIndex = 0;
		TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
		const FPCGExAttributeToTagDetails* SeedAttributesToPathTags = nullptr;
		TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

		//~ Seed quality tracking (optional)
		TArray<int8>* SeedQuality = nullptr;
		TSharedPtr<PCGExData::FPointIO> GoodSeeds;
		const FPCGExCellSeedMutationDetails* SeedMutations = nullptr;

		/**
		 * Process a cell as a path output (non-seeded variant).
		 * Paths stage by edges dataset, then by the cell's first node; InCellOrdinal breaks the tie between cells
		 * that start on the same node, so pass the cell's index whenever several cells share one collection.
		 */
		void ProcessCell(
			const TSharedPtr<FCell>& InCell,
			const TSharedPtr<PCGExData::FPointIO>& InPathIO,
			const FString& InTriageTag = TEXT(""),
			int32 InCellOrdinal = INDEX_NONE) const;

		/**
		 * Process a seeded cell as a path output.
		 * Uses cell's CustomIndex as seed index, applies seed forwarding/tagging. Paths stage by batch, then seed.
		 */
		void ProcessSeededCell(
			const TSharedPtr<FCell>& InCell,
			const TSharedPtr<PCGExData::FPointIO>& InPathIO,
			const FString& InTriageTag = TEXT("")) const;

		/** Flags the owner and merged contributors of every cell as good seeds, whatever artifact is output. Not thread-safe. */
		void MarkSeedsGood(const TArray<TSharedPtr<FCell>>& InCells) const;

	private:
		void ProcessCellInternal(
			const TSharedPtr<FCell>& InCell,
			const TSharedPtr<PCGExData::FPointIO>& InPathIO,
			const FString& InTriageTag,
			const PCGExData::FIOSortKey& InSortKey,
			int32 InSeedIndex) const;
	};
}
