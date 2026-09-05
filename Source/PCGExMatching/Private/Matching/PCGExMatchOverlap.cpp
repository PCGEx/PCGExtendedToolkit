// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchOverlap.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactoryData.h"


#define LOCTEXT_NAMESPACE "PCGExMatchOverlap"
#define PCGEX_NAMESPACE MatchOverlap


bool FPCGExMatchOverlap::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();
	const int32 NumSources = MatchableSourcesRef.Num();

	SourceBounds.Reserve(NumSources);

	if (Config.bUseMinOverlapRatio)
	{
		MinOverlapRatios.Reserve(NumSources);
	}

	// Compute overall bounds for octree initialization
	FBox OverallBounds(ForceInit);

	for (int32 i = 0; i < NumSources; i++)
	{
		const FPCGExTaggedData& TaggedData = MatchableSourcesRef[i];

		// Get bounds from data
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data);
		FBox DataBounds = SpatialData ? SpatialData->GetBounds() : FBox(ForceInit);

		// Pre-compute expanded bounds
		if (Config.ExpansionMode != EPCGExMatchOverlapExpansionMode::None && DataBounds.IsValid)
		{
			FVector ExpansionValue = FVector::ZeroVector;
			Config.Expansion.TryReadDataValue(InContext, TaggedData.Data, ExpansionValue, true);

			if (Config.ExpansionMode == EPCGExMatchOverlapExpansionMode::Add)
			{
				DataBounds = DataBounds.ExpandBy(ExpansionValue);
			}
			else // Scale mode
			{
				const FVector Center = DataBounds.GetCenter();
				const FVector Extent = DataBounds.GetExtent() * ExpansionValue;
				DataBounds = FBox(Center - Extent, Center + Extent);
			}
		}

		SourceBounds.Add(DataBounds);

		if (DataBounds.IsValid)
		{
			OverallBounds += DataBounds;
		}

		// Pre-compute min overlap ratio
		if (Config.bUseMinOverlapRatio)
		{
			double RatioValue = 0.0;
			Config.MinOverlapRatio.TryReadDataValue(InContext, TaggedData.Data, RatioValue, true);
			MinOverlapRatios.Add(RatioValue);
		}
	}

	// Build octree for spatial queries
	if (OverallBounds.IsValid && NumSources > 1)
	{
		const FVector Center = OverallBounds.GetCenter();
		const double Extent = OverallBounds.GetExtent().GetMax() * 1.1; // Slightly larger to ensure all items fit

		Octree = MakeUnique<PCGExOctree::FItemOctree>(Center, Extent);

		for (int32 i = 0; i < NumSources; i++)
		{
			if (SourceBounds[i].IsValid)
			{
				Octree->AddElement(PCGExOctree::FItem(i, FBoxSphereBounds(SourceBounds[i])));
			}
		}
	}

	return true;
}

void FPCGExMatchOverlap::GetOverlappingSourceIndices(const FBox& CandidateBounds, TArray<int32>& OutIndices) const
{
	OutIndices.Reset();

	if (!CandidateBounds.IsValid)
	{
		return;
	}

	// If no octree, fall back to returning all indices
	if (!Octree)
	{
		OutIndices.Reserve(SourceBounds.Num());
		for (int32 i = 0; i < SourceBounds.Num(); i++)
		{
			OutIndices.Add(i);
		}
		return;
	}

	// Query octree for potential overlapping sources
	Octree->FindElementsWithBoundsTest(
		CandidateBounds,
		[&OutIndices](const PCGExOctree::FItem& Item)
		{
			OutIndices.Add(Item.Index);
		});
}

double FPCGExMatchOverlap::ComputeOverlapRatio(const FBox& BoxA, const FBox& BoxB)
{
	const FBox Intersection = BoxA.Overlap(BoxB);
	if (!Intersection.IsValid)
	{
		return 0.0;
	}

	const double IntersectionVolume = Intersection.GetVolume();
	const double VolumeA = BoxA.GetVolume();
	const double VolumeB = BoxB.GetVolume();
	const double SmallestVolume = FMath::Min(VolumeA, VolumeB);

	if (SmallestVolume <= UE_SMALL_NUMBER)
	{
		return 0.0;
	}

	return IntersectionVolume / SmallestVolume;
}

bool FPCGExMatchOverlap::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	// Get the pre-computed target bounds (already expanded during preparation)
	if (!SourceBounds.IsValidIndex(InTargetElement.IO))
	{
		return Config.bInvert;
	}

	const FBox& TargetBounds = SourceBounds[InTargetElement.IO];
	if (!TargetBounds.IsValid)
	{
		return Config.bInvert;
	}

	// Get candidate bounds
	const UPCGSpatialData* CandidateSpatialData = Cast<UPCGSpatialData>(InCandidate.Data);
	if (!CandidateSpatialData)
	{
		return Config.bInvert;
	}

	const FBox CandidateBounds = CandidateSpatialData->GetBounds();

	const bool bIntersects = TargetBounds.Intersect(CandidateBounds);

	if (!bIntersects)
	{
		return Config.bInvert;
	}

	if (Config.bUseMinOverlapRatio && MinOverlapRatios.IsValidIndex(InTargetElement.IO))
	{
		const double Ratio = ComputeOverlapRatio(TargetBounds, CandidateBounds);
		const bool bMeetsThreshold = Ratio >= MinOverlapRatios[InTargetElement.IO];
		return Config.bInvert ? !bMeetsThreshold : bMeetsThreshold;
	}

	return !Config.bInvert;
}

bool UPCGExMatchOverlapFactory::WantsPoints() const
{
	return !PCGExMetaHelpers::IsDataDomainAttribute(Config.Expansion.Attribute) ||
		(Config.bUseMinOverlapRatio && !PCGExMetaHelpers::IsDataDomainAttribute(Config.MinOverlapRatio.Attribute));
}

PCGEX_MATCH_RULE_BOILERPLATE(Overlap)

#if WITH_EDITOR
FString UPCGExCreateMatchOverlapSettings::GetDisplayName() const
{
	FString Result = TEXT("Overlap");

	if (Config.bUseMinOverlapRatio)
	{
		Result += FString::Printf(TEXT(" >= %.0f%%"), Config.MinOverlapRatio.Constant * 100.0);
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
