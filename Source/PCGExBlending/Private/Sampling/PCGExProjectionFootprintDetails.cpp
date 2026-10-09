// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExProjectionFootprintDetails.h"

#include "PCGElement.h"
#include "PCGModule.h"
#include "PCGPoint.h"
#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGSpatialData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/PCGProjectionParams.h"
#include "Math/PCGExMathBounds.h"

namespace PCGExSampling
{
	bool FProjectionFootprint::Init(const FPCGExProjectionFootprintDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		Reference = InDetails.Reference;
		Axis = InDetails.Axis;
		BoundsSource = InDetails.BoundsSource;
		AdjustMode = InDetails.AdjustMode;
		FailMetric = InDetails.FailMetric;
		ThresholdSource = InDetails.ThresholdSource;
		Push = InDetails.Push;

		// Every enum is validated here so the hot path can branch on plain comparisons.
		bool bKnown = true;

		switch (Reference)
		{
		case EPCGExFootprintReference::Input:
		case EPCGExFootprintReference::Projected:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (Axis)
		{
		case EPCGExFootprintAxis::Projection:
		case EPCGExFootprintAxis::ReferenceUp:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (BoundsSource)
		{
		case EPCGExPointBoundsSource::ScaledBounds:
		case EPCGExPointBoundsSource::DensityBounds:
		case EPCGExPointBoundsSource::Bounds:
		case EPCGExPointBoundsSource::Center:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (AdjustMode)
		{
		case EPCGExFootprintAdjust::Add:
		case EPCGExFootprintAdjust::Multiply:
		case EPCGExFootprintAdjust::FixedCenter:
		case EPCGExFootprintAdjust::FixedPivot:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (FailMetric)
		{
		case EPCGExFootprintFailMetric::None:
		case EPCGExFootprintFailMetric::Overhang:
		case EPCGExFootprintFailMetric::Penetration:
		case EPCGExFootprintFailMetric::Either:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (ThresholdSource)
		{
		case EPCGExFootprintThreshold::Manual:
		case EPCGExFootprintThreshold::BoundsHeight:
			break;
		default:
			bKnown = false;
			break;
		}

		switch (Push)
		{
		case EPCGExFootprintPush::None:
		case EPCGExFootprintPush::Sink:
		case EPCGExFootprintPush::Raise:
		case EPCGExFootprintPush::Balance:
			break;
		default:
			bKnown = false;
			break;
		}

		if (!bKnown)
		{
			PCGE_LOG_C(Error, GraphAndLog, InDataFacade->GetContext(), FTEXT("Footprint : unknown enum value."));
			return false;
		}

		Adjust = InDetails.Adjust.GetValueSetting();
		if (!Adjust->Init(InDataFacade))
		{
			return false;
		}

		bConstantAdjust = Adjust->IsConstant();
		if (bConstantAdjust)
		{
			ConstantAdjust = Adjust->Read(0);
		}

		if (FailMetric != EPCGExFootprintFailMetric::None && ThresholdSource == EPCGExFootprintThreshold::Manual)
		{
			Threshold = InDetails.Threshold.GetValueSetting();
			if (!Threshold->Init(InDataFacade))
			{
				return false;
			}

			bConstantThreshold = Threshold->IsConstant();
			if (bConstantThreshold)
			{
				ConstantThreshold = Threshold->Read(0);
			}
		}

		return true;
	}

	void FProjectionFootprint::PrepareScope(const PCGExMT::FScope& Scope, FScopeView& OutView) const
	{
		if (!bConstantAdjust)
		{
			OutView.Adjusts.SetNumUninitialized(Scope.Count);
			Adjust->ReadScope(Scope.Start, OutView.Adjusts);
		}

		if (!bConstantThreshold)
		{
			OutView.Thresholds.SetNumUninitialized(Scope.Count);
			Threshold->ReadScope(Scope.Start, OutView.Thresholds);
		}
	}

	void FProjectionFootprint::Probe(const UPCGSpatialData* Target, const FPCGProjectionParams& Params, const PCGExData::FConstPoint& Point, const FVector& Center, const FQuat& Rotation, const FScopeView& View, const int32 ScopeIndex, FResult& OutResult) const
	{
		OutResult = FResult();

		const FVector& Adj = bConstantAdjust ? ConstantAdjust : View.Adjusts[ScopeIndex];

		// Fixed modes read Adjust as the extents and never touch the point bounds.
		FVector LocalCenter = FVector::ZeroVector;
		FVector Extents = Adj;

		if (AdjustMode == EPCGExFootprintAdjust::Add || AdjustMode == EPCGExFootprintAdjust::Multiply)
		{
			const FBox Box = PCGExMath::GetLocalBounds(Point, BoundsSource);
			LocalCenter = Box.GetCenter();
			Extents = AdjustMode == EPCGExFootprintAdjust::Add ? Box.GetExtent() + Adj : Box.GetExtent() * Adj;
		}

		Extents = Extents.ComponentMax(FVector::ZeroVector);

		OutResult.Height = Extents.Z * 2.0;

		const FVector Up = Rotation.GetAxisZ();
		// Fixed (Pivot) stands the box on the point origin
		const FVector BottomCenter = AdjustMode == EPCGExFootprintAdjust::FixedPivot ? FVector::ZeroVector : LocalCenter - FVector(0, 0, Extents.Z);
		const bool bProjectionAxis = Axis == EPCGExFootprintAxis::Projection;
		const FBox ProbeBox(FVector::ZeroVector, FVector::ZeroVector);

		double MinOffset = TNumericLimits<double>::Max();
		double MaxOffset = TNumericLimits<double>::Lowest();
		FVector MinDisplacement = FVector::ZeroVector;
		FVector MaxDisplacement = FVector::ZeroVector;

		for (int32 i = 0; i < 4; i++)
		{
			const FVector Corner = Center + Rotation.RotateVector(BottomCenter + FVector((i & 1) ? Extents.X : -Extents.X, (i & 2) ? Extents.Y : -Extents.Y, 0));

			FPCGPoint Projected;
			if (!Target->ProjectPoint(FTransform(Corner), ProbeBox, Params, Projected, nullptr))
			{
				OutResult.bMissed = true;
				continue;
			}

			// Positive when the surface sits above the corner (penetration), negative over a gap (overhang).
			const FVector Displacement = Projected.Transform.GetLocation() - Corner;
			const double Offset = bProjectionAxis ? Displacement.Length() * (Displacement.Dot(Up) < 0 ? -1.0 : 1.0) : Displacement.Dot(Up);

			if (Offset < MinOffset)
			{
				MinOffset = Offset;
				MinDisplacement = Displacement;
			}

			if (Offset > MaxOffset)
			{
				MaxOffset = Offset;
				MaxDisplacement = Displacement;
			}

			OutResult.Hits++;
		}

		if (OutResult.Hits == 0)
		{
			return;
		}

		OutResult.Overhang = MinOffset < 0 ? -MinOffset : 0;
		OutResult.Penetration = MaxOffset > 0 ? MaxOffset : 0;

		if (bProjectionAxis)
		{
			OutResult.RawLo = MinDisplacement;
			OutResult.RawHi = MaxDisplacement;
			OutResult.Lo = MinOffset < 0 ? MinDisplacement : FVector::ZeroVector;
			OutResult.Hi = MaxOffset > 0 ? MaxDisplacement : FVector::ZeroVector;
		}
		else
		{
			OutResult.RawLo = Up * MinOffset;
			OutResult.RawHi = Up * MaxOffset;
			OutResult.Lo = Up * -OutResult.Overhang;
			OutResult.Hi = Up * OutResult.Penetration;
		}
	}

	bool FProjectionFootprint::ShouldFail(const FResult& Result, const FScopeView& View, const int32 ScopeIndex) const
	{
		if (FailMetric == EPCGExFootprintFailMetric::None)
		{
			return false;
		}

		// A corner with no surface under it is an unbounded overhang.
		if (Result.bMissed)
		{
			return true;
		}

		const double Limit = ThresholdSource == EPCGExFootprintThreshold::BoundsHeight ? Result.Height : (bConstantThreshold ? ConstantThreshold : View.Thresholds[ScopeIndex]);

		switch (FailMetric)
		{
		case EPCGExFootprintFailMetric::Overhang:
			return Result.Overhang > Limit;
		case EPCGExFootprintFailMetric::Penetration:
			return Result.Penetration > Limit;
		case EPCGExFootprintFailMetric::Either:
			return Result.Overhang > Limit || Result.Penetration > Limit;
		default:
			// Init rejects unknown metrics
			checkNoEntry();
			return false;
		}
	}

	FVector FProjectionFootprint::GetPush(const FResult& Result) const
	{
		switch (Push)
		{
		case EPCGExFootprintPush::None:
			return FVector::ZeroVector;
		case EPCGExFootprintPush::Sink:
			return Result.Lo;
		case EPCGExFootprintPush::Raise:
			return Result.Hi;
		case EPCGExFootprintPush::Balance:
			return (Result.RawLo + Result.RawHi) * 0.5;
		default:
			// Init rejects unknown modes
			checkNoEntry();
			return FVector::ZeroVector;
		}
	}
}
