// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExExtrusion.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"

namespace PCGExExtrusion
{
	//
	// FExtrusionConfig Implementation
	//

	void FExtrusionConfig::ComputeFlags(bool bHasStopFilters, bool bHasExternalPaths)
	{
		uint32 FlagBits = 0;
		if (bAllowChildExtrusions)
		{
			FlagBits |= static_cast<uint32>(EExtrusionFlags::AllowsChildren);
		}
		if (bDetectClosedLoops)
		{
			FlagBits |= static_cast<uint32>(EExtrusionFlags::ClosedLoop);
		}
		if (bHasStopFilters)
		{
			FlagBits |= static_cast<uint32>(EExtrusionFlags::Bounded);
		}
		if (bHasExternalPaths || bDoSelfIntersections)
		{
			FlagBits |= static_cast<uint32>(EExtrusionFlags::CollisionCheck);
		}

		Flags = static_cast<EExtrusionFlags>(FlagBits);
	}

	void FExtrusionConfig::InitIntersectionDetails()
	{
		ExternalPathIntersections.Init();
		SelfPathIntersections.Init();
		MergeDetails.Init();
	}

	//
	// FExtrusion Implementation
	//

	FExtrusion::FExtrusion(const int32 InSeedIndex, const TSharedRef<PCGExData::FFacade>& InFacade, const int32 InMaxIterations, const FExtrusionConfig& InConfig)
		: Config(InConfig)
		  , SeedIndex(InSeedIndex)
		  , RemainingIterations(InMaxIterations)
		  , PointDataFacade(InFacade)
	{
		ExtrudedPoints.Reserve(InMaxIterations);
		Origin = InFacade->Source->GetInPoint(SeedIndex);
		ProxyHead = PCGExData::FProxyPoint(Origin);
		ExtrudedPoints.Emplace(Origin.GetTransform());
		Flags = Config.Flags;
		SetHead(ExtrudedPoints.Last());
	}

	void FExtrusion::SetHead(const FTransform& InHead)
	{
		LastInsertion = InHead.GetLocation();
		Head = InHead;
		ProxyHead.Transform = InHead;
		ExtrudedPoints.Last() = Head;
		Metrics = PCGExPaths::FPathMetrics(LastInsertion);

		// Use 0 tolerance during initial construction (when no segments exist yet)
		const double Tol = SegmentBounds.Num() > 0 ? Config.SelfPathIntersections.Tolerance : 0;
		PCGEX_SET_BOX_TOLERANCE(Bounds, (Metrics.Last + FVector::OneVector * 1), (Metrics.Last + FVector::OneVector * -1), Tol);
	}

	PCGExMath::FSegment FExtrusion::GetHeadSegment() const
	{
		return PCGExMath::FSegment(ExtrudedPoints.Last(1).GetLocation(), ExtrudedPoints.Last().GetLocation(), Config.ExternalPathIntersections.Tolerance);
	}

	PCGExMath::FSegment FExtrusion::GetCurrentHeadSegment() const
	{
		return PCGExMath::FSegment(ExtrudedPoints.Last().GetLocation(), Head.GetLocation(), Config.SelfPathIntersections.Tolerance);
	}

	bool FExtrusion::Advance()
	{
		if (State == EExtrusionState::Stopped || State == EExtrusionState::Completed)
		{
			return false;
		}

		bAdvancedOnly = true;

		const FVector PreviousHeadLocation = Head.GetLocation();

		// Sample tensor field
		bool bSuccess = false;
		const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(SeedIndex, Head, bSuccess);

		if (!bSuccess)
		{
			Stop(EStopReason::SamplingFailed);
			return false;
		}

		ExtrusionDirection = Sample.DirectionAndSize.GetSafeNormal();

		// Apply rotation based on configuration
		ApplyRotation(Sample);

		// Move head forward
		const FVector HeadLocation = PreviousHeadLocation + Sample.DirectionAndSize;
		Head.SetLocation(HeadLocation);
		ActiveTransform = Head;

		// Check for closed loop (if enabled)
		if (HasFlag(Flags, EExtrusionFlags::ClosedLoop))
		{
			if (CheckClosedLoop(PreviousHeadLocation))
			{
				Stop(EStopReason::ClosedLoop);
				return false;
			}
		}

		// Check stop filters (if bounded)
		if (HasFlag(Flags, EExtrusionFlags::Bounded))
		{
			ProxyHead.Transform = ActiveTransform;

			if (CheckStopFilters())
			{
				// Hit stop filter
				if (State == EExtrusionState::Extruding)
				{
					if (Config.StopHandling == EPCGExTensorStopConditionHandling::Include)
					{
						Insert(Head);
					}

					Complete();

					if (!HasFlag(Flags, EExtrusionFlags::AllowsChildren))
					{
						Stop(EStopReason::StopFilter);
						return false;
					}
				}

				// Still probing or completed - continue advancing
				RemainingIterations--;
				return RemainingIterations > 0;
			}

			// We're out of stop filter region
			if (State == EExtrusionState::Completed)
			{
				// Re-entered valid region after completion - start child
				if (HasFlag(Flags, EExtrusionFlags::AllowsChildren))
				{
					StartNewExtrusion();
				}
				Stop(EStopReason::StopFilter);
				return false;
			}

			// Transition from probing to extruding
			if (State == EExtrusionState::Probing)
			{
				State = EExtrusionState::Extruding;
				bIsExtruding = true;
				SetHead(Head);
				RemainingIterations--;
				return RemainingIterations > 0;
			}
		}
		else
		{
			// No stop filters - immediately start extruding
			if (State == EExtrusionState::Probing)
			{
				State = EExtrusionState::Extruding;
				bIsExtruding = true;
			}
		}

		State = EExtrusionState::Extruding;
		bIsExtruding = true;

		// Track distance for fuse check
		double DistToLast = 0;
		const double Length = Metrics.Add(Metrics.Last + Sample.DirectionAndSize, DistToLast);
		DistToLastSum += DistToLast;

		// Skip if too close to last point
		if (DistToLastSum < Config.FuseDistance)
		{
			RemainingIterations--;
			return RemainingIterations > 0;
		}
		DistToLastSum = 0;

		// Adjust position if exceeding max length
		if (Length > MaxLength)
		{
			const FVector LastValidPos = ExtrudedPoints.Last().GetLocation();
			ActiveTransform.SetLocation(LastValidPos + ((Metrics.Last - LastValidPos).GetSafeNormal() * (MaxLength - (Length - DistToLast))));
		}

		// Check for collisions (if enabled)
		if (HasFlag(Flags, EExtrusionFlags::CollisionCheck))
		{
			const PCGExMath::FSegment Segment(ExtrudedPoints.Last().GetLocation(), ActiveTransform.GetLocation());

			if (CheckCollisions(Segment))
			{
				return false;
			}
		}

		// Insert the point
		Insert(ActiveTransform);

		// Check termination conditions
		RemainingIterations--;

		if (RemainingIterations <= 0)
		{
			Stop(EStopReason::Iterations);
			return false;
		}

		if (Length >= MaxLength)
		{
			Stop(EStopReason::MaxLength);
			return false;
		}

		if (ExtrudedPoints.Num() >= MaxPointCount)
		{
			Stop(EStopReason::MaxPointCount);
			return false;
		}

		return true;
	}

	void FExtrusion::ApplyRotation(const PCGExTensor::FTensorSample& Sample)
	{
		if (!Config.bTransformRotation)
		{
			return;
		}

		switch (Config.RotationMode)
		{
		case EPCGExTensorTransformMode::Absolute:
			Head.SetRotation(Sample.Rotation);
			break;

		case EPCGExTensorTransformMode::Relative:
			Head.SetRotation(Head.GetRotation() * Sample.Rotation);
			break;

		case EPCGExTensorTransformMode::Align:
			Head.SetRotation(PCGExMath::MakeDirection(Config.AlignAxis, ExtrusionDirection, Head.GetRotation().GetUpVector()));
			break;
		}
	}

	bool FExtrusion::CheckClosedLoop(const FVector& PreviousHeadLocation)
	{
		const FVector Tail = Origin.GetLocation();

		if (FVector::DistSquared(Metrics.Last, Tail) > Config.ClosedLoopSquaredDistance)
		{
			return false;
		}

		const FVector DirectionToTail = (Tail - PreviousHeadLocation).GetSafeNormal();
		return FVector::DotProduct(ExtrusionDirection, DirectionToTail) > Config.ClosedLoopSearchDot;
	}

	bool FExtrusion::CheckStopFilters()
	{
		if (!StopFilters)
		{
			return false;
		}
		return StopFilters->Test(ProxyHead);
	}

	bool FExtrusion::CheckCollisions(const PCGExMath::FSegment& Segment)
	{
		// Check external path intersections first
		FCollisionResult ExternalResult = CheckExternalIntersection(Segment);

		if (ExternalResult)
		{
			// Handle origin intersection ignore
			if (FMath::IsNearlyZero(ExternalResult.Position.SquaredLength() - Origin.GetLocation().SquaredLength()))
			{
				if (Config.bIgnoreIntersectionOnOrigin && ExtrudedPoints.Num() <= 1)
				{
					ExternalResult.bHasCollision = false;
				}
			}

			if (ExternalResult)
			{
				ActiveTransform.SetLocation(ExternalResult.Position);
				Insert(ActiveTransform);
				Stop(EStopReason::ExternalPath);
				return true;
			}
		}

		// Check self path intersections
		PCGExMath::FClosestPosition MergePosition(Segment.Lerp(Config.ProximitySegmentBalance));
		FCollisionResult SelfResult = CheckSelfIntersection(Segment, MergePosition);

		// Create merge collision result if applicable
		FCollisionResult MergeResult;
		if (TryMerge(Segment, MergePosition))
		{
			MergeResult.Set(MergePosition, EStopReason::SelfMerge);
		}

		// Resolve priority between crossing and merge
		FCollisionResult FinalResult = ResolveCollisionPriority(SelfResult, MergeResult);

		if (FinalResult)
		{
			ActiveTransform.SetLocation(FinalResult.Position);
			Insert(ActiveTransform);
			Stop(FinalResult.Reason);
			return true;
		}

		return false;
	}

	FCollisionResult FExtrusion::CheckExternalIntersection(const PCGExMath::FSegment& Segment)
	{
		FCollisionResult Result;

		if (!Config.bDoExternalIntersections || !ExternalPaths || ExternalPaths->IsEmpty())
		{
			return Result;
		}

		int32 PathIndex = -1;
		PCGExMath::FClosestPosition Intersection = PCGExPaths::Helpers::FindClosestIntersection(
			*ExternalPaths,
			Config.ExternalPathIntersections,
			Segment,
			PathIndex);

		if (Intersection)
		{
			Result.Set(Intersection, EStopReason::ExternalPath, PathIndex);
		}

		return Result;
	}

	FCollisionResult FExtrusion::CheckSelfIntersection(const PCGExMath::FSegment& Segment, PCGExMath::FClosestPosition& OutMerge)
	{
		FCollisionResult Result;

		if (!Config.bDoSelfIntersections || !SolidPaths)
		{
			return Result;
		}

		int32 PathIndex = -1;
		// Note: Uses ExternalPathIntersections for SolidPaths check, not SelfPathIntersections
		PCGExMath::FClosestPosition Intersection = PCGExPaths::Helpers::FindClosestIntersection(
			*SolidPaths.Get(),
			Config.ExternalPathIntersections,
			Segment,
			PathIndex,
			OutMerge);

		if (Intersection)
		{
			Result.Set(Intersection, EStopReason::SelfIntersection, PathIndex);
		}

		return Result;
	}

	FCollisionResult FExtrusion::ResolveCollisionPriority(const FCollisionResult& Crossing, const FCollisionResult& Merge)
	{
		if (!Crossing && !Merge)
		{
			return FCollisionResult();
		}
		if (Crossing && !Merge)
		{
			return Crossing;
		}
		if (!Crossing && Merge)
		{
			return Merge;
		}

		// Both exist - crossing takes priority by default
		// Owner can configure this via Config if needed
		return Crossing;
	}

	bool FExtrusion::TryMerge(const PCGExMath::FSegment& InSegment, const PCGExMath::FClosestPosition& InMerge)
	{
		if (!Config.bMergeOnProximity || !InMerge)
		{
			return false;
		}

		if (Config.MergeDetails.bWantsDotCheck)
		{
			if (!Config.MergeDetails.CheckDot(FMath::Abs(FVector::DotProduct(InMerge.Direction(), InSegment.Direction))))
			{
				return false;
			}
		}

		return InMerge.DistSquared <= Config.MergeDetails.ToleranceSquared;
	}

	PCGExMath::FClosestPosition FExtrusion::FindCrossing(const PCGExMath::FSegment& InSegment, bool& OutIsLastSegment, PCGExMath::FClosestPosition& OutClosestPosition, const int32 TruncateSearch) const
	{
		if (!Bounds.Intersect(InSegment.Bounds))
		{
			return PCGExMath::FClosestPosition();
		}

		const int32 MaxSearches = SegmentBounds.Num() - TruncateSearch;
		if (MaxSearches <= 0)
		{
			return PCGExMath::FClosestPosition();
		}

		PCGExMath::FClosestPosition Crossing(InSegment.A);

		const int32 LastSegment = SegmentBounds.Num() - 1;
		const double SqrTolerance = Config.SelfPathIntersections.ToleranceSquared;
		const uint8 Strictness = Config.SelfPathIntersections.Strictness;

		for (int32 i = 0; i < MaxSearches; i++)
		{
			if (!SegmentBounds[i].Intersect(InSegment.Bounds))
			{
				continue;
			}

			FVector A = ExtrudedPoints[i].GetLocation();
			FVector B = ExtrudedPoints[i + 1].GetLocation();

			if (Config.SelfPathIntersections.bWantsDotCheck)
			{
				if (!Config.SelfPathIntersections.CheckDot(FMath::Abs(FVector::DotProduct((B - A).GetSafeNormal(), InSegment.Direction))))
				{
					continue;
				}
			}

			FVector OutSelf = FVector::ZeroVector;
			FVector OutOther = FVector::ZeroVector;

			if (!InSegment.FindIntersection(A, B, SqrTolerance, OutSelf, OutOther, Strictness))
			{
				OutClosestPosition.Update(OutOther);
				continue;
			}

			OutClosestPosition.Update(OutOther);
			Crossing.Update(OutOther, i);
		}

		OutIsLastSegment = Crossing.Index == LastSegment;
		return Crossing;
	}

	void FExtrusion::Insert(const FTransform& InPoint)
	{
		bAdvancedOnly = false;

		ExtrudedPoints.Emplace(InPoint);
		LastInsertion = InPoint.GetLocation();

		if (Config.bDoSelfIntersections)
		{
			PCGEX_BOX_TOLERANCE(OEBox, ExtrudedPoints.Last(1).GetLocation(), ExtrudedPoints.Last().GetLocation(), (Config.SelfPathIntersections.ToleranceSquared + 1));
			SegmentBounds.Emplace(OEBox);
			Bounds += OEBox;
		}
	}

	void FExtrusion::StartNewExtrusion()
	{
		if (RemainingIterations > 1 && Callbacks.OnCreateChild)
		{
			if (TSharedPtr<FExtrusion> ChildExtrusion = Callbacks.OnCreateChild(SharedThis(this)))
			{
				ChildExtrusion->bIsChildExtrusion = true;
				ChildExtrusion->bIsFollowUp = (State == EExtrusionState::Completed);
			}
		}
	}

	void FExtrusion::Stop(EStopReason InReason)
	{
		StopReason |= InReason;
		Complete();
		State = EExtrusionState::Stopped;
	}

	void FExtrusion::Complete()
	{
		if (State == EExtrusionState::Completed || State == EExtrusionState::Stopped)
		{
			return;
		}

		State = EExtrusionState::Completed;

		// Validate path - use callback if available, otherwise default to >= 2 points
		const bool bIsValid = Callbacks.OnValidatePath
			? Callbacks.OnValidatePath(ExtrudedPoints.Num())
			: (ExtrudedPoints.Num() >= 2);

		if (!bIsValid)
		{
			PointDataFacade->Source->InitializeOutput(PCGExData::EIOInit::NoInit);
			PointDataFacade->Source->Disable();
			return;
		}

		// Write output data
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		PCGExPaths::Helpers::SetClosedLoop(OutPointData, HasStopReason(EStopReason::ClosedLoop));

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPointData, ExtrudedPoints.Num(), PointDataFacade->GetAllocations());

		TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange();
		for (int32 i = 0; i < ExtrudedPoints.Num(); i++)
		{
			OutTransforms[i] = ExtrudedPoints[i];
		}

		if (Callbacks.OnApplyTags)
		{
			Callbacks.OnApplyTags(*this, *PointDataFacade->Source);
		}

		PointDataFacade->Source->GetOutKeys(true);
	}

	void FExtrusion::CutOff(const FVector& InCutOff)
	{
		// Check if cutoff connects back to last point or point before last (self-connection)
		const FVector LastPos = ExtrudedPoints.Last().GetLocation();
		const FVector PrevPos = ExtrudedPoints.Last(1).GetLocation();

		const double DistToLast = FVector::DistSquared(InCutOff, LastPos);
		const double DistToPrev = FVector::DistSquared(InCutOff, PrevPos);

		if (DistToLast <= Config.FuseDistanceSquared || DistToPrev <= Config.FuseDistanceSquared)
		{
			// Cutoff connects back to own path - remove last point and stop
			ExtrudedPoints.Pop();
			if (SegmentBounds.Num() > 0)
			{
				SegmentBounds.Pop();
			}

			StopReason |= EStopReason::SelfIntersection;
			Complete();
			State = EExtrusionState::Stopped;
			return;
		}

		ExtrudedPoints.Last().SetLocation(InCutOff);
		StopReason |= EStopReason::SelfIntersection;

		Complete();

		PCGEX_BOX_TOLERANCE(OEBox, PrevPos, ExtrudedPoints.Last().GetLocation(), (Config.SelfPathIntersections.ToleranceSquared + 1));
		if (SegmentBounds.Num() > 0)
		{
			SegmentBounds.Last() = OEBox;
			Bounds += OEBox; // Ensure overall bounds include the cutoff segment
		}

		State = EExtrusionState::Stopped;
	}

	void FExtrusion::Shorten(const FVector& InCutOff)
	{
		const FVector A = ExtrudedPoints.Last(1).GetLocation();
		if (FVector::DistSquared(A, InCutOff) < FVector::DistSquared(A, ExtrudedPoints.Last().GetLocation()))
		{
			ExtrudedPoints.Last().SetLocation(InCutOff);
		}
	}

	void FExtrusion::Cleanup()
	{
		SegmentBounds.Empty();
	}
}
