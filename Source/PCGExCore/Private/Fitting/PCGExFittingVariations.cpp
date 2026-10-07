// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Fitting/PCGExFittingVariations.h"

#include "Math/PCGExMath.h"
#include "Math/PCGExMathAxis.h"

void FPCGExFittingVariations::ApplyOffset(const FRandomStream& RandomStream, FTransform& OutTransform) const
{
	const FVector BaseLocation = OutTransform.GetLocation();
	const FQuat BaseRotation = OutTransform.GetRotation();

	FVector RandomOffset = FVector(RandomStream.FRandRange(OffsetMin.X, OffsetMax.X), RandomStream.FRandRange(OffsetMin.Y, OffsetMax.Y), RandomStream.FRandRange(OffsetMin.Z, OffsetMax.Z));

	if (SnapPosition == EPCGExVariationSnapping::SnapOffset)
	{
		PCGExMath::Snap(RandomOffset, OffsetSnap);
	}

	FVector OutLocation = bAbsoluteOffset ? BaseLocation + RandomOffset : BaseLocation + BaseRotation.RotateVector(RandomOffset);

	if (SnapPosition == EPCGExVariationSnapping::SnapResult)
	{
		PCGExMath::Snap(OutLocation, OffsetSnap);
	}

	OutTransform.SetLocation(OutLocation);
}

void FPCGExFittingVariations::ApplyRotation(const FRandomStream& RandomStream, FTransform& OutTransform) const
{
	const FQuat BaseRotation = OutTransform.GetRotation();

	FRotator RandRot = FRotator(RandomStream.FRandRange(RotationMin.Pitch, RotationMax.Pitch), RandomStream.FRandRange(RotationMin.Yaw, RotationMax.Yaw), RandomStream.FRandRange(RotationMin.Roll, RotationMax.Roll));

	if (SnapRotation == EPCGExVariationSnapping::SnapOffset)
	{
		PCGExMath::Snap(RandRot, RotationSnap);
	}

	FRotator OutRotation = BaseRotation.Rotator();

	if (AbsoluteRotation & static_cast<uint8>(EPCGExAbsoluteRotationFlags::X))
	{
		OutRotation.Roll = RandRot.Roll;
	}
	else
	{
		OutRotation.Roll += RandRot.Roll;
	}

	if (AbsoluteRotation & static_cast<uint8>(EPCGExAbsoluteRotationFlags::Y))
	{
		OutRotation.Pitch = RandRot.Pitch;
	}
	else
	{
		OutRotation.Pitch += RandRot.Pitch;
	}

	if (AbsoluteRotation & static_cast<uint8>(EPCGExAbsoluteRotationFlags::Z))
	{
		OutRotation.Yaw = RandRot.Yaw;
	}
	else
	{
		OutRotation.Yaw += RandRot.Yaw;
	}

	if (SnapRotation == EPCGExVariationSnapping::SnapResult)
	{
		PCGExMath::Snap(OutRotation, RotationSnap);
	}

	OutTransform.SetRotation(OutRotation.Quaternion());
}

void FPCGExFittingVariations::ApplyScale(const FRandomStream& RandomStream, FTransform& OutTransform) const
{
	FVector OutScale = OutTransform.GetScale3D();
	FVector RandomScale;
	if (bUniformScale)
	{
		const double S = RandomStream.FRandRange(ScaleMin.X, ScaleMax.X);
		RandomScale = FVector(S);
	}
	else
	{
		RandomScale = FVector(RandomStream.FRandRange(ScaleMin.X, ScaleMax.X), RandomStream.FRandRange(ScaleMin.Y, ScaleMax.Y), RandomStream.FRandRange(ScaleMin.Z, ScaleMax.Z));
	}

	if (SnapScale == EPCGExVariationSnapping::SnapOffset)
	{
		PCGExMath::Snap(RandomScale, ScaleSnap);
	}

	OutScale *= RandomScale;

	if (SnapScale == EPCGExVariationSnapping::SnapResult)
	{
		PCGExMath::Snap(OutScale, ScaleSnap);
	}

	OutTransform.SetScale3D(OutScale);
}
