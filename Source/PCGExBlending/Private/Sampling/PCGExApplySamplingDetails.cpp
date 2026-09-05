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
}

void FPCGExApplySamplingDetails::Apply(PCGExData::FMutablePoint& InPoint, const FTransform& InTransform, const FTransform& InLookAt)
{
	FTransform& T = InPoint.GetMutableTransform();

	FVector OutRotation = T.GetRotation().Euler();
	FVector OutPosition = T.GetLocation();
	FVector OutScale = T.GetScale3D();

	if (bApplyTransform)
	{
		const FVector InTrRot = InTransform.GetRotation().Euler();
		for (const int32 C : TrRotComponents)
		{
			OutRotation[C] = InTrRot[C];
		}

		FVector InTrPos = InTransform.GetLocation();
		for (const int32 C : TrPosComponents)
		{
			OutPosition[C] = InTrPos[C];
		}

		FVector InTrSca = InTransform.GetScale3D();
		for (const int32 C : TrScaComponents)
		{
			OutScale[C] = InTrSca[C];
		}
	}

	if (bApplyLookAt)
	{
		const FVector InLkRot = InLookAt.GetRotation().Euler();
		for (const int32 C : LkRotComponents)
		{
			OutRotation[C] = InLkRot[C];
		}

	}

	T = FTransform(FQuat::MakeFromEuler(OutRotation), OutPosition, OutScale);
}
