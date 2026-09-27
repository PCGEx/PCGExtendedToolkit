// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExCellPathBuilder.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Data/Utils/PCGExDataForwardDetails.h"

namespace PCGExClusters
{
	void FCellPathBuilder::ProcessCell(
		const TSharedPtr<FCell>& InCell,
		const TSharedPtr<PCGExData::FPointIO>& InPathIO,
		const FString& InTriageTag,
		const int32 InCellOrdinal) const
	{
		if (!InCell || !InPathIO || !Cluster)
		{
			return;
		}

		PCGExData::FIOSortKey SortKey;
		if (EdgeDataFacade) { SortKey = SortKey.Derived(EdgeDataFacade->Source->IOIndex); }
		SortKey = SortKey.Derived(Cluster->GetNodePointIndex(InCell->Nodes[0]));
		if (InCellOrdinal != INDEX_NONE) { SortKey = SortKey.Derived(InCellOrdinal); }

		ProcessCellInternal(InCell, InPathIO, InTriageTag, SortKey, INDEX_NONE);
	}

	void FCellPathBuilder::ProcessSeededCell(
		const TSharedPtr<FCell>& InCell,
		const TSharedPtr<PCGExData::FPointIO>& InPathIO,
		const FString& InTriageTag) const
	{
		if (!InCell || !InPathIO || !Cluster)
		{
			return;
		}

		const int32 SeedIndex = InCell->CustomIndex;

		// Batch, then seed; BatchIndex restarts per vtx input, so the vtx dataset breaks that tie.
		const TSharedPtr<PCGExData::FPointIO> VtxIO = Cluster->VtxIO.Pin();
		const PCGExData::FIOSortKey SortKey{BatchIndex, SeedIndex, VtxIO ? VtxIO->IOIndex : 0};

		ProcessCellInternal(InCell, InPathIO, InTriageTag, SortKey, SeedIndex);
	}

	void FCellPathBuilder::ProcessCellInternal(
		const TSharedPtr<FCell>& InCell,
		const TSharedPtr<PCGExData::FPointIO>& InPathIO,
		const FString& InTriageTag,
		const PCGExData::FIOSortKey& InSortKey,
		int32 InSeedIndex) const
	{
		const int32 NumCellPoints = InCell->Nodes.Num();

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(InPathIO->GetOut(), NumCellPoints);

		// Setup tags
		if (!InTriageTag.IsEmpty())
		{
			InPathIO->Tags->AddRaw(InTriageTag);
		}

		InPathIO->SetSortKey(InSortKey);

		Helpers::CleanupClusterData(InPathIO);

		// Create facade for attribute writing
		PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, InPathIO.ToSharedRef())

		// Build read indices from cell nodes
		TArray<int32> ReadIndices;
		ReadIndices.SetNumUninitialized(NumCellPoints);
		for (int32 i = 0; i < NumCellPoints; i++)
		{
			ReadIndices[i] = Cluster->GetNodePointIndex(InCell->Nodes[i]);
		}

		// Copy points from cluster
		InPathIO->InheritPoints(ReadIndices, 0);

		// Post-process (handles winding, leaf duplication, etc.)
		InCell->PostProcessPoints(InPathIO->GetOut());

		// Seed-specific processing
		if (InSeedIndex != INDEX_NONE && SeedsDataFacade)
		{
			// Apply seed-to-path tagging
			if (SeedAttributesToPathTags)
			{
				SeedAttributesToPathTags->Tag(SeedsDataFacade->GetInPoint(InSeedIndex), InPathIO);
			}

			if (SeedForwardHandler)
			{
				SeedForwardHandler->Forward(InSeedIndex, PathDataFacade);
			}
		}

		// Write artifacts (cell hash, area, compactness, etc.)
		if (Artifacts)
		{
			Artifacts->Process(Cluster, PathDataFacade, InCell);
		}

		// Commit writes
		PathDataFacade->WriteFastest(TaskManager);

		// Seed mutations only; good-seed flags are set by the processor through MarkSeedsGood.
		if (InSeedIndex != INDEX_NONE && SeedQuality && GoodSeeds && SeedMutations)
		{
			PCGExData::FMutablePoint SeedPoint = GoodSeeds->GetOutPoint(InSeedIndex);
			SeedMutations->ApplyToPoint(InCell.Get(), SeedPoint, InPathIO->GetOut());
		}
	}

	void FCellPathBuilder::MarkSeedsGood(const TArray<TSharedPtr<FCell>>& InCells) const
	{
		if (!SeedQuality)
		{
			return;
		}

		TArray<int8>& Quality = *SeedQuality;
		for (const TSharedPtr<FCell>& Cell : InCells)
		{
			if (!Cell)
			{
				continue;
			}

			if (Quality.IsValidIndex(Cell->CustomIndex))
			{
				Quality[Cell->CustomIndex] = true;
			}

			for (const int32 Contributor : Cell->ContributorIndices)
			{
				if (Quality.IsValidIndex(Contributor))
				{
					Quality[Contributor] = true;
				}
			}
		}
	}
}
