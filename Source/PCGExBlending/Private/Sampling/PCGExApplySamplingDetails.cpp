// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExApplySamplingDetails.h"

#include "Data/PCGExPointElements.h"
#include "Sampling/PCGExSamplingCommon.h"

bool FPCGExApplySamplingDetails::WantsApply() const
{
	return AppliedComponents > 0;
}

void FPCGExApplySamplingDetails::Init()
{
#define PCGEX_REGISTER_FLAG(_COMPONENT, _ARRAY) \
if ((_COMPONENT & static_cast<uint8>(EPCGExApplySampledComponentFlags::X)) != 0){ _ARRAY.Add(0); AppliedComponents++; } \
if ((_COMPONENT & static_cast<uint8>(EPCGExApplySampledComponentFlags::Y)) != 0){ _ARRAY.Add(1); AppliedComponents++; } \
if ((_COMPONENT & static_cast<uint8>(EPCGExApplySampledComponentFlags::Z)) != 0){ _ARRAY.Add(2); AppliedComponents++; }

	if (bApplyTransform)
	{
		PCGEX_REGISTER_FLAG(TransformPosition, TrPosComponents)
		PCGEX_REGISTER_FLAG(TransformRotation, TrRotComponents)
		PCGEX_REGISTER_FLAG(TransformScale, TrScaComponents)
	}

	if (bApplyLookAt)
	{
		PCGEX_REGISTER_FLAG(LookAtRotation, LkRotComponents)
	}

#undef PCGEX_REGISTER_FLAG

	bFullPosition = TrPosComponents.Num() == 3;
	bFullRotation = TrRotComponents.Num() == 3;
	bFullScale = TrScaComponents.Num() == 3;
	bFullLookAt = LkRotComponents.Num() == 3;
}

void FPCGExApplySamplingDetails::Apply(FTransform& InOutTransform, const FTransform& InTransform, const FTransform& InLookAt) const
{
	if (bApplyTransform)
	{
		if (bFullPosition)
		{
			InOutTransform.SetLocation(InTransform.GetLocation());
		}
		else if (!TrPosComponents.IsEmpty())
		{
			FVector OutPosition = InOutTransform.GetLocation();
			const FVector InTrPos = InTransform.GetLocation();
			for (const int32 C : TrPosComponents)
			{
				OutPosition[C] = InTrPos[C];
			}
			InOutTransform.SetLocation(OutPosition);
		}

		if (bFullScale)
		{
			InOutTransform.SetScale3D(InTransform.GetScale3D());
		}
		else if (!TrScaComponents.IsEmpty())
		{
			FVector OutScale = InOutTransform.GetScale3D();
			const FVector InTrSca = InTransform.GetScale3D();
			for (const int32 C : TrScaComponents)
			{
				OutScale[C] = InTrSca[C];
			}
			InOutTransform.SetScale3D(OutScale);
		}
	}

	// Look-at components override transform components; a full look-at makes the transform rotation moot.
	const bool bLookAtFull = bApplyLookAt && bFullLookAt;
	const bool bLookAtPartial = bApplyLookAt && !bFullLookAt && !LkRotComponents.IsEmpty();
	const bool bTrRotFull = bApplyTransform && bFullRotation;
	const bool bTrRotPartial = bApplyTransform && !bFullRotation && !TrRotComponents.IsEmpty();

	if (bLookAtFull)
	{
		InOutTransform.SetRotation(InLookAt.GetRotation());
	}
	else if (bLookAtPartial || bTrRotPartial)
	{
		FVector OutRotation = (bTrRotFull ? InTransform : InOutTransform).GetRotation().Euler();

		if (bTrRotPartial)
		{
			const FVector InTrRot = InTransform.GetRotation().Euler();
			for (const int32 C : TrRotComponents)
			{
				OutRotation[C] = InTrRot[C];
			}
		}

		if (bLookAtPartial)
		{
			const FVector InLkRot = InLookAt.GetRotation().Euler();
			for (const int32 C : LkRotComponents)
			{
				OutRotation[C] = InLkRot[C];
			}
		}

		InOutTransform.SetRotation(FQuat::MakeFromEuler(OutRotation));
	}
	else if (bTrRotFull)
	{
		InOutTransform.SetRotation(InTransform.GetRotation());
	}
}

void FPCGExApplySamplingDetails::Apply(PCGExData::FMutablePoint& InPoint, const FTransform& InTransform, const FTransform& InLookAt) const
{
	FTransform OutTransform = InPoint.GetTransform();
	Apply(OutTransform, InTransform, InLookAt);
	InPoint.SetTransform(OutTransform);
}
