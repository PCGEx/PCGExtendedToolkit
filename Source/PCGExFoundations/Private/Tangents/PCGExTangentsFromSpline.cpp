// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tangents/PCGExTangentsFromSpline.h"

#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Data/PCGSplineData.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineStruct.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMath.h"

namespace PCGExTangentsFromSpline
{
	// Key spans below this are collapsed: both ends resolve to the same reference location.
	constexpr double KeyEpsilon = 1.0e-4;

	// Full key range of a closed loop (last key + loop offset); only meaningful when the spline is closed.
	double LoopSpan(const FPCGSplineStruct& InSpline)
	{
		return InSpline.GetInputKeyAtSegmentStart(InSpline.GetNumberOfPoints());
	}
}

#pragma region FPCGExTangentsFromSpline

bool FPCGExTangentsFromSpline::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (!FPCGExTangentsOperation::PrepareForData(InContext, InDataFacade))
	{
		return false;
	}

	const int32 NumPoints = InDataFacade->GetNum();
	NumSources = Sources.Num();
	BestSource.Init(-1, NumPoints);

	if (NumSources == 0)
	{
		// Nothing to sample; every point uses the neighbor fallback. The factory already warned.
		return true;
	}

	Keys.SetNumUninitialized(NumPoints * NumSources);

	TSharedPtr<PCGExDetails::TSettingValue<double>> MaxDistanceReader;
	if (bUseMaxDistance)
	{
		MaxDistanceReader = MaxDistance.GetValueSetting();
		// Whole-buffer read: this runs before any scoped fetch, and the parallel pre-pass below reads it from any thread.
		if (!MaxDistanceReader->Init(InDataFacade, false))
		{
			return false;
		}
	}

	const TConstPCGValueRange<FTransform> InTransforms = InDataFacade->GetIn()->GetConstTransformValueRange();

	// Blocks until done; each iteration owns its own Keys row and BestSource slot.
	PCGExMT::ParallelOrSequentialScoped(NumPoints, [&](const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(i)
		{
			const FVector Location = InTransforms[i].GetLocation();
			double BestDistSquared = MaxDistanceReader ? FMath::Square(MaxDistanceReader->Read(i)) : TNumericLimits<double>::Max();
			float* PointKeys = Keys.GetData() + i * NumSources;

			for (int32 s = 0; s < NumSources; s++)
			{
				const FPCGSplineStruct& Spline = *Sources[s];
				const float Key = Spline.FindInputKeyClosestToWorldLocation(Location);
				PointKeys[s] = Key;

				const double DistSquared = FVector::DistSquared(Location, Spline.GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));
				if (DistSquared > BestDistSquared)
				{
					continue;
				}

				BestDistSquared = DistSquared;
				BestSource[i] = s;
			}
		}
	});

	return true;
}

void FPCGExTangentsFromSpline::ProcessFirstPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
{
	const int32 SourceIndex = BestSource[0];
	if (SourceIndex >= 0)
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const FPCGSplineStruct& Spline = *Sources[SourceIndex];
		const float Key = KeyOn(SourceIndex, 0);
		const double Delta = KeyDelta(Spline, Key, KeyOn(SourceIndex, 1), InTransforms[0].GetLocation(), InTransforms[1].GetLocation());

		if (FMath::Abs(Delta) > PCGExTangentsFromSpline::KeyEpsilon)
		{
			// Single neighbour: both tangents span toward it.
			const FVector Dir = Spline.GetTangentAtSplineInputKey(Key, ESplineCoordinateSpace::World) * Delta;
			OutArrive = Dir * ArriveScale;
			OutLeave = Dir * LeaveScale;
			return;
		}
	}

	FPCGExTangentsOperation::ProcessFirstPoint(InPointData, ArriveScale, OutArrive, LeaveScale, OutLeave);
}

void FPCGExTangentsFromSpline::ProcessLastPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
{
	const int32 LastIndex = InPointData->GetNumPoints() - 1;
	const int32 SourceIndex = BestSource[LastIndex];
	if (SourceIndex >= 0)
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const FPCGSplineStruct& Spline = *Sources[SourceIndex];
		const float Key = KeyOn(SourceIndex, LastIndex);
		const double Delta = KeyDelta(Spline, KeyOn(SourceIndex, LastIndex - 1), Key, InTransforms[LastIndex - 1].GetLocation(), InTransforms[LastIndex].GetLocation());

		if (FMath::Abs(Delta) > PCGExTangentsFromSpline::KeyEpsilon)
		{
			const FVector Dir = Spline.GetTangentAtSplineInputKey(Key, ESplineCoordinateSpace::World) * Delta;
			OutArrive = Dir * ArriveScale;
			OutLeave = Dir * LeaveScale;
			return;
		}
	}

	FPCGExTangentsOperation::ProcessLastPoint(InPointData, ArriveScale, OutArrive, LeaveScale, OutLeave);
}

void FPCGExTangentsFromSpline::ProcessPoint(const UPCGBasePointData* InPointData, const int32 Index, const int32 NextIndex, const int32 PrevIndex, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
{
	const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
	const int32 SourceIndex = BestSource[Index];

	if (SourceIndex >= 0)
	{
		const FPCGSplineStruct& Spline = *Sources[SourceIndex];
		const FVector Location = InTransforms[Index].GetLocation();
		const float Key = KeyOn(SourceIndex, Index);
		const double DeltaArrive = KeyDelta(Spline, KeyOn(SourceIndex, PrevIndex), Key, InTransforms[PrevIndex].GetLocation(), Location);
		const double DeltaLeave = KeyDelta(Spline, Key, KeyOn(SourceIndex, NextIndex), Location, InTransforms[NextIndex].GetLocation());

		if (FMath::Abs(DeltaArrive) > PCGExTangentsFromSpline::KeyEpsilon || FMath::Abs(DeltaLeave) > PCGExTangentsFromSpline::KeyEpsilon)
		{
			Resolve(Spline, Key, DeltaArrive, DeltaLeave, ArriveScale, OutArrive, LeaveScale, OutLeave);
			return;
		}
	}

	// No usable reference: half the neighbour chord, same as the Catmull-Rom and From Neighbors modules.
	const FVector Dir = (InTransforms[NextIndex].GetLocation() - InTransforms[PrevIndex].GetLocation()) * 0.5;
	OutArrive = Dir * ArriveScale;
	OutLeave = Dir * LeaveScale;
}

double FPCGExTangentsFromSpline::KeyDelta(const FPCGSplineStruct& InSpline, const float FromKey, const float ToKey, const FVector& FromLocation, const FVector& ToLocation) const
{
	const double Delta = static_cast<double>(ToKey) - static_cast<double>(FromKey);
	if (!InSpline.IsClosedLoop())
	{
		return Delta;
	}

	const double Span = PCGExTangentsFromSpline::LoopSpan(InSpline);
	const double Shortest = PCGExMath::Tile(Delta, -Span * 0.5, Span * 0.5);
	if (FMath::Abs(Shortest) <= PCGExTangentsFromSpline::KeyEpsilon)
	{
		return Shortest;
	}

	// Shortest way round the loop unless the path chord clearly travels the other way (sparse samples per loop).
	const double Along = FVector::DotProduct(InSpline.GetTangentAtSplineInputKey(FromKey, ESplineCoordinateSpace::World), ToLocation - FromLocation);
	if (FMath::IsNearlyZero(Along) || (Along > 0) == (Shortest > 0))
	{
		return Shortest;
	}

	return Shortest > 0 ? Shortest - Span : Shortest + Span;
}

FVector FPCGExTangentsFromSpline::Derivative(const FPCGSplineStruct& InSpline, float Key, const bool bArriveSide) const
{
	// The arrive side reads the left limit so a reference corner (arrive != leave) keeps both directions. Only a key
	// sitting on a control point needs it; the step is relative so it stays representable at any key magnitude.
	if (bArriveSide)
	{
		const float Nearest = FMath::RoundToFloat(Key);
		if (FMath::IsNearlyEqual(Key, Nearest, static_cast<float>(PCGExTangentsFromSpline::KeyEpsilon)))
		{
			Key = Nearest - FMath::Max(static_cast<float>(PCGExTangentsFromSpline::KeyEpsilon), Nearest * 1.0e-6f);
			if (Key < 0)
			{
				Key = InSpline.IsClosedLoop() ? static_cast<float>(PCGExTangentsFromSpline::LoopSpan(InSpline)) : 0;
			}
		}
	}

	return InSpline.GetTangentAtSplineInputKey(Key, ESplineCoordinateSpace::World);
}

void FPCGExTangentsFromSpline::Resolve(const FPCGSplineStruct& InSpline, const float Key, double DeltaArrive, double DeltaLeave, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
{
	// A collapsed side borrows the other's span so the point keeps a continuous tangent instead of a cusp.
	if (FMath::Abs(DeltaArrive) <= PCGExTangentsFromSpline::KeyEpsilon)
	{
		DeltaArrive = DeltaLeave;
	}
	if (FMath::Abs(DeltaLeave) <= PCGExTangentsFromSpline::KeyEpsilon)
	{
		DeltaLeave = DeltaArrive;
	}

	OutArrive = Derivative(InSpline, Key, true) * DeltaArrive * ArriveScale;
	OutLeave = Derivative(InSpline, Key, false) * DeltaLeave * LeaveScale;
}

#pragma endregion

#pragma region UPCGExFromSplineTangents

void UPCGExFromSplineTangents::InitializeInContext(FPCGExContext* InContext, const FName InOverridesPinLabel)
{
	Super::InitializeInContext(InContext, InOverridesPinLabel);

	Sources.Reset();
	for (const FPCGTaggedData& TaggedData : InContext->InputData.GetInputsByPin(PCGExTangents::SourceTangentSourcesLabel))
	{
		const UPCGSplineData* SplineData = Cast<UPCGSplineData>(TaggedData.Data);
		if (!SplineData || SplineData->SplineStruct.GetNumberOfSplineSegments() <= 0)
		{
			continue;
		}

		Sources.Add(&SplineData->SplineStruct);
	}

	if (Sources.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("From Spline tangents: no usable spline on the Tangent Sources pin, falling back to neighbor-based tangents."));
	}
}

void UPCGExFromSplineTangents::Cleanup()
{
	Sources.Reset();
	Super::Cleanup();
}

TSharedPtr<FPCGExTangentsOperation> UPCGExFromSplineTangents::CreateOperation() const
{
	PCGEX_FACTORY_NEW_OPERATION(TangentsFromSpline)
	NewOperation->Sources = Sources;
	NewOperation->bUseMaxDistance = bUseMaxDistance;
	NewOperation->MaxDistance = MaxDistance;
	return NewOperation;
}

#pragma endregion
