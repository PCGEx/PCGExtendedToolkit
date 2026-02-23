// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExTargetsHandler.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExTaggedData.h"
#include "Details/PCGExDistancesDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Math/PCGExMathDistances.h"

namespace PCGExMatching
{
	int32 FTargetsHandler::Init(FPCGExContext* InContext, const FName InPinLabel, FInitData&& InitFn)
	{
		FBox OctreeBounds = FBox(ForceInit);

		TSharedPtr<PCGExData::FPointIOCollection> Targets = MakeShared<PCGExData::FPointIOCollection>(InContext, InPinLabel, PCGExData::EIOInit::NoInit, true);

		if (Targets->IsEmpty())
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No targets (empty datasets)"))
			return 0;
		}

		TargetFacades.Reserve(Targets->Pairs.Num());
		TargetOctrees.Reserve(Targets->Pairs.Num());

		TArray<FBox> Bounds;
		Bounds.Reserve(Targets->Pairs.Num());

		int32 Idx = 0;
		for (const TSharedPtr<PCGExData::FPointIO>& IO : Targets->Pairs)
		{
			const FBox DataBounds = InitFn(IO, Idx);
			if (!DataBounds.IsValid) { continue; }

			TSharedPtr<PCGExData::FFacade> TargetFacade = MakeShared<PCGExData::FFacade>(IO.ToSharedRef());

			TargetFacade->Idx = Idx;
			TargetFacades.Add(TargetFacade.ToSharedRef());
			TargetOctrees.Add(&TargetFacade->GetIn()->GetPointOctree());

			MaxNumTargets = FMath::Max(MaxNumTargets, TargetFacade->GetNum());

			Bounds.Emplace(DataBounds);
			OctreeBounds += DataBounds;

			Idx++;
		}

		if (TargetFacades.IsEmpty()) { return 0; }

		TargetsOctree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
		for (int i = 0; i < TargetFacades.Num(); ++i) { TargetsOctree->AddElement(PCGExOctree::FItem(i, Bounds[i])); }

		TargetsPreloader = MakeShared<PCGExData::FMultiFacadePreloader>(TargetFacades);

		return TargetFacades.Num();
	}

	int32 FTargetsHandler::Init(FPCGExContext* InContext, const FName InPinLabel)
	{
		return Init(InContext, InPinLabel, [](const TSharedPtr<PCGExData::FPointIO>& IO, const int32 Idx) { return IO->GetIn()->GetBounds(); });
	}

	void FTargetsHandler::SetDistances(const FPCGExDistanceDetails& InDetails)
	{
		Distances = InDetails.MakeDistances();
	}

	void FTargetsHandler::SetDistances(const EPCGExDistance Source, const EPCGExDistance Target, const bool bOverlapIsZero)
	{
		Distances = PCGExMath::GetDistances(Source, Target, bOverlapIsZero);
	}

	void FTargetsHandler::SetMatchingDetails(FPCGExContext* InContext, const FPCGExMatchingDetails* InDetails)
	{
		DataMatcher = MakeShared<FDataMatcher>();
		DataMatcher->SetDetails(InDetails);
		if (!DataMatcher->Init(InContext, TargetFacades, false)) { DataMatcher.Reset(); }
	}

	bool FTargetsHandler::PopulateIgnoreList(const TSharedPtr<PCGExData::FPointIO>& InDataCandidate, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const
	{
		if (DataMatcher) { return DataMatcher->PopulateIgnoreList(InDataCandidate->GetTaggedData(), InMatchingScope, OutIgnoreList); }
		return true;
	}

	bool FTargetsHandler::PopulateIgnoreListInverse(const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& InMatchRuleFactories, const TSharedPtr<PCGExData::FFacade>& InSourceFacade, const FPCGExMatchingDetails* InDetails, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const
	{
		if (!InDetails || !InDetails->IsEnabled()) { return true; }
		if (InMatchRuleFactories.IsEmpty()) { return true; }

		// Create inverse matcher: input is the single MatchableSource, targets are candidates
		auto InverseMatcher = MakeShared<FDataMatcher>();
		InverseMatcher->SetDetails(InDetails);

		TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
		SingleSource.Add(InSourceFacade);
		if (!InverseMatcher->Init(InMatchRuleFactories, SingleSource, false)) { return true; }

		// Build FPCGExTaggedData array from targets (candidates in inverse context)
		TArray<FPCGExTaggedData> TargetCandidates;
		TargetCandidates.Reserve(TargetFacades.Num());
		for (int32 i = 0; i < TargetFacades.Num(); i++)
		{
			TargetCandidates.Add(TargetFacades[i]->Source->GetTaggedData(PCGExData::EIOSide::In, i));
		}

		return InverseMatcher->PopulateIgnoreListFromCandidates(TargetCandidates, InMatchingScope, OutIgnoreList);
	}

	bool FTargetsHandler::HandleUnmatchedOutput(const TSharedPtr<PCGExData::FFacade>& InFacade, const bool bForward) const
	{
		if (DataMatcher) { return DataMatcher->HandleUnmatchedOutput(InFacade, bForward); }
		if (bForward) { return InFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward); }
		return false;
	}


	void FTargetsHandler::ForEachPreloader(PCGExData::FMultiFacadePreloader::FPreloaderItCallback&& It) const
	{
		TargetsPreloader->ForEach(MoveTemp(It));
	}

	void FTargetsHandler::ForEachTarget(FFacadeRefIterator&& It, const TSet<const UPCGData*>* Exclude) const
	{
		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[i];
			if (Exclude && Exclude->Contains(Target->GetIn())) { continue; }
			It(Target, i);
		}
	}

	bool FTargetsHandler::ForEachTarget(FFacadeRefIteratorWithBreak&& It, const TSet<const UPCGData*>* Exclude) const
	{
		bool bBreak = false;
		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[i];
			if (Exclude && Exclude->Contains(Target->GetIn())) { continue; }
			It(Target, i, bBreak);
			if (bBreak) { return true; }
		}

		return bBreak;
	}

	void FTargetsHandler::ForEachTargetPoint(FPointIterator&& It, const TSet<const UPCGData*>* Exclude) const
	{
		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			if (Exclude && Exclude->Contains(TargetFacades[i]->GetIn())) { continue; }
			const int32 NumPoints = TargetFacades[i]->GetNum();
			for (int j = 0; j < NumPoints; j++) { It(PCGExData::FPoint(j, i)); }
		}
	}

	void FTargetsHandler::ForEachTargetPoint(FPointIteratorWithData&& It, const TSet<const UPCGData*>* Exclude) const
	{
		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[i];
			if (Exclude && Exclude->Contains(Target->GetIn())) { continue; }
			const int32 NumPoints = TargetFacades[i]->GetNum();
			for (int j = 0; j < NumPoints; j++)
			{
				PCGExData::FConstPoint Point = Target->GetInPoint(j);
				Point.IO = i;
				It(Point);
			}
		}
	}

	void FTargetsHandler::FindTargetsWithBoundsTest(const FBoxCenterAndExtent& QueryBounds, FTargetQuery&& Func, const TSet<const UPCGData*>* Exclude) const
	{
		TargetsOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
		{
			if (Exclude && Exclude->Contains(TargetFacades[Item.Index]->GetIn())) { return; }
			Func(Item);
		});
	}

	void FTargetsHandler::FindElementsWithBoundsTest(const FBoxCenterAndExtent& QueryBounds, FPointIteratorWithData&& Func, const TSet<const UPCGData*>* Exclude) const
	{
		TargetsOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
		{
			const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
			if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

			TargetOctrees[Item.Index]->FindElementsWithBoundsTest(QueryBounds, [&](const PCGPointOctree::FPointRef& PointRef)
			{
				PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);
				Point.IO = Item.Index;
				Func(Point);
			});
		});
	}

	bool FTargetsHandler::FindClosestTarget(const PCGExData::FConstPoint& Probe, const FBoxCenterAndExtent& QueryBounds, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude) const
	{
		bool bFound = false;

		if (Distances->bOverlapIsZero)
		{
			TargetsOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
			{
				const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
				const bool bSelf = Target->GetIn() == Probe.Data;

				if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

				TargetOctrees[Item.Index]->FindElementsWithBoundsTest(QueryBounds, [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (bSelf && PointRef.Index == Probe.Index) { return; }

					PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);
					bool bOverlap = false;
					double Dist = Distances->GetDistSquared(Probe, Point, bOverlap);
					if (bOverlap) { Dist = 0; }

					if (OutDistSquared > Dist)
					{
						OutResult = Point;
						OutResult.IO = Item.Index;

						OutDistSquared = Dist;
						bFound = true;
					}
				});
			});
		}
		else
		{
			TargetsOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
			{
				const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
				const bool bSelf = Target->GetIn() == Probe.Data;

				if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

				TargetOctrees[Item.Index]->FindElementsWithBoundsTest(QueryBounds, [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (bSelf && PointRef.Index == Probe.Index) { return; }

					const PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);
					if (const double Dist = Distances->GetDistSquared(Probe, Point); OutDistSquared > Dist)
					{
						OutResult = Point;
						OutResult.IO = Item.Index;

						OutDistSquared = Dist;
						bFound = true;
					}
				});
			});
		}

		return bFound;
	}

	void FTargetsHandler::FindClosestTarget(const PCGExData::FConstPoint& Probe, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude) const
	{
		const FVector ProbeLocation = Probe.GetLocation();

		if (Distances->bOverlapIsZero)
		{
			TargetsOctree->FindNearbyElements(ProbeLocation, [&](const PCGExOctree::FItem& Item)
			{
				const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
				const bool bSelf = Target->GetIn() == Probe.Data;

				if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

				TargetOctrees[Item.Index]->FindNearbyElements(ProbeLocation, [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (bSelf && PointRef.Index == Probe.Index) { return; }

					const PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);
					bool bOverlap = false;
					double Dist = Distances->GetDistSquared(Probe, Point, bOverlap);
					if (bOverlap) { Dist = 0; }

					if (OutDistSquared > Dist)
					{
						OutResult = Point;
						OutResult.IO = Item.Index;

						OutDistSquared = Dist;
					}
				});
			});
		}
		else
		{
			TargetsOctree->FindNearbyElements(ProbeLocation, [&](const PCGExOctree::FItem& Item)
			{
				const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
				const bool bSelf = Target->GetIn() == Probe.Data;

				if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

				TargetOctrees[Item.Index]->FindNearbyElements(ProbeLocation, [&](const PCGPointOctree::FPointRef& PointRef)
				{
					if (bSelf && PointRef.Index == Probe.Index) { return; }

					const PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);
					if (const double Dist = Distances->GetDistSquared(Probe, Point); OutDistSquared > Dist)
					{
						OutResult = Point;
						OutResult.IO = Item.Index;

						OutDistSquared = Dist;
					}
				});
			});
		}
	}

	void FTargetsHandler::FindClosestTarget(const FVector& Probe, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude) const
	{
		TargetsOctree->FindNearbyElements(Probe, [&](const PCGExOctree::FItem& Item)
		{
			const TSharedRef<PCGExData::FFacade>& Target = TargetFacades[Item.Index];
			if (Exclude && Exclude->Contains(Target->GetIn())) { return; }

			TargetOctrees[Item.Index]->FindNearbyElements(Probe, [&](const PCGPointOctree::FPointRef& PointRef)
			{
				PCGExData::FConstPoint Point = Target->GetInPoint(PointRef.Index);

				if (const double Dist = FVector::DistSquared(Distances->GetTargetCenter(Point, Point.GetLocation(), Probe), Probe); OutDistSquared > Dist)
				{
					OutResult = Point;
					OutResult.IO = Item.Index;

					OutDistSquared = Dist;
				}
			});
		});
	}

	PCGExData::FConstPoint FTargetsHandler::GetPoint(const int32 IO, const int32 Index) const
	{
		return TargetFacades[IO]->GetInPoint(Index);
	}

	PCGExData::FConstPoint FTargetsHandler::GetPoint(const PCGExData::FPoint& Point) const
	{
		return TargetFacades[Point.IO]->GetInPoint(Point.Index);
	}

	int32 FTargetsHandler::GetTotalTargetPointCount(const TSet<const UPCGData*>* Exclude) const
	{
		int32 Total = 0;
		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			if (Exclude && Exclude->Contains(TargetFacades[i]->GetIn())) { continue; }
			Total += TargetFacades[i]->GetNum();
		}
		return Total;
	}

	void FTargetsHandler::BuildFlatTargetMap(TArray<int32>& OutPrefixSums, const TSet<const UPCGData*>* Exclude) const
	{
		OutPrefixSums.SetNum(TargetFacades.Num() + 1);
		OutPrefixSums[0] = 0;

		for (int i = 0; i < TargetFacades.Num(); i++)
		{
			const int32 Count = (Exclude && Exclude->Contains(TargetFacades[i]->GetIn())) ? 0 : TargetFacades[i]->GetNum();
			OutPrefixSums[i + 1] = OutPrefixSums[i] + Count;
		}
	}

	PCGExData::FConstPoint FTargetsHandler::GetPointByFlatIndex(const int32 FlatIndex, const TArray<int32>& PrefixSums, const TSet<const UPCGData*>* Exclude) const
	{
		// Binary search to find the IO index
		int32 IOIndex = 0;
		for (int i = 1; i < PrefixSums.Num(); i++)
		{
			if (FlatIndex < PrefixSums[i])
			{
				IOIndex = i - 1;
				break;
			}
		}

		// Skip excluded facades
		while (Exclude && Exclude->Contains(TargetFacades[IOIndex]->GetIn()))
		{
			IOIndex++;
		}

		const int32 PointIndex = FlatIndex - PrefixSums[IOIndex];

		PCGExData::FConstPoint Point = TargetFacades[IOIndex]->GetInPoint(PointIndex);
		Point.IO = IOIndex;
		return Point;
	}

	double FTargetsHandler::GetDistSquared(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint) const
	{
		if (Distances->bOverlapIsZero)
		{
			bool bOverlap = false;
			double DistSquared = Distances->GetDistSquared(SourcePoint, TargetPoint, bOverlap);
			if (bOverlap) { DistSquared = 0; }
			return DistSquared;
		}
		return Distances->GetDistSquared(SourcePoint, TargetPoint);
	}

	FVector FTargetsHandler::GetSourceCenter(const PCGExData::FPoint& OriginPoint, const FVector& OriginLocation, const FVector& ToCenter) const
	{
		return Distances->GetSourceCenter(OriginPoint, OriginLocation, ToCenter);
	}

	void FTargetsHandler::StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle) const
	{
		PCGEX_SCHEDULING_SCOPE(TaskManager,);
		TargetsPreloader->StartLoading(TaskManager, InParentHandle);
	}
}
