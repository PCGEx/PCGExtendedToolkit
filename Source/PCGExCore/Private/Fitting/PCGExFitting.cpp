// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Fitting/PCGExFitting.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Fitting/PCGExFittingVariations.h"
#include "Math/PCGExMathBounds.h"

void FPCGExScaleToFitDetails::Process(const PCGExData::FPoint& InPoint, const FBox& InBounds, FVector& OutScale, FBox& OutBounds) const
{
	PCGExFitting::ScaleToFitAxes(
		ScaleToFitMode, ScaleToFit, ScaleToFitX, ScaleToFitY, ScaleToFitZ,
		InPoint.GetLocalBounds().GetSize(), InPoint.GetTransform().GetScale3D(),
		InBounds, OutScale, OutBounds);
}

FPCGExSingleJustifyDetails::FPCGExSingleJustifyDetails()
{
}

bool FPCGExSingleJustifyDetails::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	if (!SharedFromGetter)
	{
		FromGetter = CustomFrom.GetValueSetting();
		if (!FromGetter->Init(InDataFacade))
		{
			return false;
		}
	}

	if (To == EPCGExJustifyTo::Same)
	{
		switch (From)
		{
		default: case EPCGExJustifyFrom::Min:
			To = EPCGExJustifyTo::Min;
			break;
		case EPCGExJustifyFrom::Center:
			To = EPCGExJustifyTo::Center;
			break;
		case EPCGExJustifyFrom::Max:
			To = EPCGExJustifyTo::Max;
			break;
		case EPCGExJustifyFrom::Custom:
			break;
		case EPCGExJustifyFrom::Pivot:
			To = EPCGExJustifyTo::Pivot;
			break;
		}
	}

	if (!SharedToGetter)
	{
		ToGetter = CustomTo.GetValueSetting();
		if (!ToGetter->Init(InDataFacade))
		{
			return false;
		}
	}

	return true;
}

void FPCGExSingleJustifyDetails::JustifyAxis(const int32 Axis, const int32 Index, const FVector& InCenter, const FVector& InSize, const FVector& OutCenter, const FVector& OutSize, FVector& OutTranslation) const
{
	const double FromValue = SharedFromGetter ? SharedFromGetter->Read(Index)[Axis] : FromGetter->Read(Index);
	const double ToValue = SharedToGetter ? SharedToGetter->Read(Index)[Axis] : ToGetter->Read(Index);

	PCGExFitting::JustifyAxis(From, To, FromValue, ToValue, Axis, InCenter, InSize, OutCenter, OutSize, OutTranslation);
}

void FPCGExJustificationDetails::Process(const int32 Index, const FBox& InBounds, const FBox& OutBounds, FVector& OutTranslation) const
{
	const FVector InCenter = InBounds.GetCenter();
	const FVector InSize = InBounds.GetSize();

	const FVector OutCenter = OutBounds.GetCenter();
	const FVector OutSize = OutBounds.GetSize();

	if (bDoJustifyX)
	{
		JustifyX.JustifyAxis(0, Index, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
	if (bDoJustifyY)
	{
		JustifyY.JustifyAxis(1, Index, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
	if (bDoJustifyZ)
	{
		JustifyZ.JustifyAxis(2, Index, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
}

bool FPCGExJustificationDetails::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	if (bSharedCustomFromAttribute)
	{
		SharedFromGetter = CustomFrom.GetValueSetting();
		if (!SharedFromGetter->Init(InDataFacade))
		{
			return false;
		}
	}

	if (bSharedCustomToAttribute)
	{
		SharedToGetter = CustomTo.GetValueSetting();
		if (!SharedToGetter->Init(InDataFacade))
		{
			return false;
		}
	}

	if (bDoJustifyX)
	{
		if (JustifyX.From == EPCGExJustifyFrom::Pivot && (JustifyX.To == EPCGExJustifyTo::Pivot || JustifyX.To == EPCGExJustifyTo::Same))
		{
			bDoJustifyX = false;
		}
		else
		{
			JustifyX.SharedFromGetter = SharedFromGetter;
			JustifyX.SharedToGetter = SharedToGetter;
			if (!JustifyX.Init(InContext, InDataFacade))
			{
				return false;
			}
		}
	}

	if (bDoJustifyY)
	{
		if (JustifyY.From == EPCGExJustifyFrom::Pivot && (JustifyY.To == EPCGExJustifyTo::Pivot || JustifyY.To == EPCGExJustifyTo::Same))
		{
			bDoJustifyY = false;
		}
		else
		{
			JustifyY.SharedFromGetter = SharedFromGetter;
			JustifyY.SharedToGetter = SharedToGetter;
			if (!JustifyY.Init(InContext, InDataFacade))
			{
				return false;
			}
		}
	}

	if (bDoJustifyZ)
	{
		if (JustifyZ.From == EPCGExJustifyFrom::Pivot && (JustifyZ.To == EPCGExJustifyTo::Pivot || JustifyZ.To == EPCGExJustifyTo::Same))
		{
			bDoJustifyZ = false;
		}
		else
		{
			JustifyZ.SharedFromGetter = SharedFromGetter;
			JustifyZ.SharedToGetter = SharedToGetter;
			if (!JustifyZ.Init(InContext, InDataFacade))
			{
				return false;
			}
		}
	}
	return true;
}

void FPCGExFittingVariationsDetails::Init(const int InSeed)
{
	Seed = InSeed;
	bEnabledBefore = (Offset == EPCGExVariationMode::Before || Rotation == EPCGExVariationMode::Before || Scale == EPCGExVariationMode::Before);
	bEnabledAfter = (Offset == EPCGExVariationMode::After || Rotation == EPCGExVariationMode::After || Scale == EPCGExVariationMode::After);
}

void FPCGExFittingVariationsDetails::Apply(const FRandomStream& RandomStream, FTransform& OutTransform, const FPCGExFittingVariations& Variations, const EPCGExVariationMode& Step) const
{
	if (Offset == Step)
	{
		Variations.ApplyOffset(RandomStream, OutTransform);
	}
	if (Rotation == Step)
	{
		Variations.ApplyRotation(RandomStream, OutTransform);
	}
	if (Scale == Step)
	{
		Variations.ApplyScale(RandomStream, OutTransform);
	}
}

bool FPCGExFittingDetailsHandler::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InTargetFacade)
{
	TargetDataFacade = InTargetFacade;
	return Justification.Init(InContext, InTargetFacade);
}

void FPCGExFittingDetailsHandler::ComputeTransform(const int32 TargetIndex, FTransform& OutTransform, FBox& InOutBounds, FVector& OutTranslation, const bool bWorldSpace, const PCGExFitting::FOverridesView& InOverrides) const
{
	check(TargetDataFacade);
	const PCGExData::FConstPoint& TargetPoint = TargetDataFacade->Source->GetInPoint(TargetIndex);

	if (bWorldSpace)
	{
		OutTransform = TargetPoint.GetTransform();
	}

	FVector OutScale = OutTransform.GetScale3D();
	OutTranslation = FVector::ZeroVector;

	if (InOverrides.ScaleToFit)
	{
		InOverrides.ScaleToFit->Process(TargetPoint, InOutBounds, OutScale, InOutBounds);
	}
	else
	{
		ScaleToFit.Process(TargetPoint, InOutBounds, OutScale, InOutBounds);
	}

	const FBox TargetBounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(TargetPoint);
	const FBox FittedBounds = FBox(InOutBounds.Min * OutScale, InOutBounds.Max * OutScale);

	if (InOverrides.Justification)
	{
		InOverrides.Justification->Process(TargetBounds, FittedBounds, OutTranslation);
	}
	else
	{
		Justification.Process(TargetIndex, TargetBounds, FittedBounds, OutTranslation);
	}

	OutTransform.AddToTranslation(OutTransform.GetRotation().RotateVector(OutTranslation));
	OutTransform.SetScale3D(OutScale);
}

void FPCGExFittingDetailsHandler::ComputeLocalTransform(const int32 TargetIndex, const FTransform& InLocalXForm, FTransform& OutTransform, FBox& InOutBounds, FVector& OutTranslation, const PCGExFitting::FOverridesView& InOverrides) const
{
	// Computes a final world transform for placing a candidate asset at a target point,
	// incorporating: (1) the candidate's local pre-rotation/scale, (2) scale-to-fit against
	// the target point's bounds, (3) justification alignment, and (4) the target's world transform.
	// The pipeline is: local scale → fit → rotate AABB for justification → compose world transform.
	check(TargetDataFacade);
	const PCGExData::FConstPoint& TargetPoint = TargetDataFacade->Source->GetInPoint(TargetIndex);
	const FTransform& TargetTransform = TargetPoint.GetTransform();

	const FVector LocalScale = InLocalXForm.GetScale3D();
	const FQuat LocalRotation = InLocalXForm.GetRotation();
	const FVector LocalTranslation = InLocalXForm.GetTranslation();

	FVector OutScale = TargetTransform.GetScale3D();
	OutTranslation = FVector::ZeroVector;

	// FITTING: Use only-scaled bounds to compute correct per-axis scale factors
	const FBox ScaledBounds(InOutBounds.Min * LocalScale, InOutBounds.Max * LocalScale);
	if (InOverrides.ScaleToFit)
	{
		InOverrides.ScaleToFit->Process(TargetPoint, ScaledBounds, OutScale, InOutBounds);
	}
	else
	{
		ScaleToFit.Process(TargetPoint, ScaledBounds, OutScale, InOutBounds);
	}

	// JUSTIFICATION: Compute where the rotated asset will actually be positioned
	// Start with fitted bounds (scaled by both local scale and fitting scale)
	FBox JustificationBounds(InOutBounds.Min * OutScale, InOutBounds.Max * OutScale);

	// Apply local rotation to get final AABB (this expansion is correct for justification)
	if (!LocalRotation.IsIdentity())
	{
		JustificationBounds = JustificationBounds.TransformBy(FTransform(LocalRotation));
	}

	if (InOverrides.Justification)
	{
		InOverrides.Justification->Process(
			PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(TargetPoint),
			JustificationBounds,
			OutTranslation);
	}
	else
	{
		Justification.Process(
			TargetIndex,
			PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(TargetPoint),
			JustificationBounds,
			OutTranslation);
	}

	// Update output bounds to reflect the final AABB
	InOutBounds = JustificationBounds;

	// Build final transform
	OutTransform = TargetTransform;
	OutTransform.AddToTranslation(TargetTransform.GetRotation().RotateVector(OutTranslation));
	OutTransform.SetScale3D(OutScale);
	OutTransform.SetRotation(TargetTransform.GetRotation() * LocalRotation);

	// Apply local offset in final rotated space
	if (!LocalTranslation.IsNearlyZero())
	{
		OutTransform.AddToTranslation(OutTransform.GetRotation().RotateVector(LocalTranslation));
	}
}

bool FPCGExFittingDetailsHandler::WillChangeBounds() const
{
	return ScaleToFit.ScaleToFitMode != EPCGExFitMode::None;
}

bool FPCGExFittingDetailsHandler::WillChangeTransform() const
{
	return ScaleToFit.ScaleToFitMode != EPCGExFitMode::None || Justification.bDoJustifyX || Justification.bDoJustifyY || Justification.bDoJustifyZ;
}
