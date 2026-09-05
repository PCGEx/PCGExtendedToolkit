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
		const FString& InTriageTag) const
	{
		if (!InCell || !InPathIO || !Cluster)
		{
			return;
		}

		// Edge-based IOIndex for non-seeded variants
		const int32 IOIndex = EdgeDataFacade
			? EdgeDataFacade->Source->IOIndex * 1000000 + Cluster->GetNodePointIndex(InCell->Nodes[0])
			: Cluster->GetNodePointIndex(InCell->Nodes[0]);

		ProcessCellInternal(InCell, InPathIO, InTriageTag, IOIndex, INDEX_NONE);
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
		const int32 IOIndex = BatchIndex * 1000000 + SeedIndex;

		ProcessCellInternal(InCell, InPathIO, InTriageTag, IOIndex, SeedIndex);
	}

	void FCellPathBuilder::ProcessCellInternal(
		const TSharedPtr<FCell>& InCell,
		const TSharedPtr<PCGExData::FPointIO>& InPathIO,
		const FString& InTriageTag,
		int32 InIOIndex,
		int32 InSeedIndex) const
	{
		const int32 NumCellPoints = InCell->Nodes.Num();

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(InPathIO->GetOut(), NumCellPoints);

		// Setup tags
		if (!InTriageTag.IsEmpty())
		{
			InPathIO->Tags->AddRaw(InTriageTag);
		}

		// Set IO index
		InPathIO->IOIndex = InIOIndex;

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

		// Handle seed quality tracking and mutations
		if (InSeedIndex != INDEX_NONE && SeedQuality && GoodSeeds && SeedMutations)
		{
			(*SeedQuality)[InSeedIndex] = true;
			PCGExData::FMutablePoint SeedPoint = GoodSeeds->GetOutPoint(InSeedIndex);
			SeedMutations->ApplyToPoint(InCell.Get(), SeedPoint, InPathIO->GetOut());
		}
	}
}
