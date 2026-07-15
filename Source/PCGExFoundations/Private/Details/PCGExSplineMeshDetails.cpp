// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExSplineMeshDetails.h"

#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Details/PCGExSettingsDetails.h"

namespace PCGExPaths
{
	void GetAxisForEntry(const FPCGExStaticMeshComponentDescriptor& InDescriptor, ESplineMeshAxis::Type& OutAxis, int32& OutC1, int32& OutC2, const EPCGExSplineMeshAxis Default)
	{
		EPCGExSplineMeshAxis Axis = InDescriptor.SplineMeshAxis;
		if (Axis == EPCGExSplineMeshAxis::Default)
		{
			Axis = Default;
		}

		switch (Axis)
		{
		default: case EPCGExSplineMeshAxis::Default:
		case EPCGExSplineMeshAxis::X:
			OutAxis = ESplineMeshAxis::X;
			OutC1 = 1;
			OutC2 = 2;
			break;
		case EPCGExSplineMeshAxis::Y:
			OutC1 = 0;
			OutC2 = 2;
			OutAxis = ESplineMeshAxis::Y;
			break;
		case EPCGExSplineMeshAxis::Z:
			OutC1 = 1;
			OutC2 = 0;
			OutAxis = ESplineMeshAxis::Z;
			break;
		}
	}

	void FSplineMeshSegment::ComputeUpVectorFromTangents()
	{
		// Thanks Drakynfly @ https://www.reddit.com/r/unrealengine/comments/kqo6ez/usplinecomponent_twists_in_on_itself/

		const FVector A = Params.StartTangent.GetSafeNormal(0.001);
		const FVector B = Params.EndTangent.GetSafeNormal(0.001);
		if (const float Dot = A | B;
			Dot > 0.99 || Dot <= -0.99)
		{
			UpVector = FVector(A.Y, A.Z, A.X);
		}
		else
		{
			UpVector = A ^ B;
		}
	}

	void FSplineMeshSegment::ApplySettings(USplineMeshComponent* Component) const
	{
		check(Component)

		Component->SetStartAndEnd(Params.StartPos, Params.StartTangent, Params.EndPos, Params.EndTangent, false);

		Component->SetStartScale(Params.StartScale, false);
		if (bUseDegrees)
		{
			Component->SetStartRollDegrees(Params.StartRoll, false);
		}
		else
		{
			Component->SetStartRoll(Params.StartRoll, false);
		}

		Component->SetEndScale(Params.EndScale, false);
		if (bUseDegrees)
		{
			Component->SetEndRollDegrees(Params.EndRoll, false);
		}
		else
		{
			Component->SetEndRoll(Params.EndRoll, false);
		}

		Component->SetForwardAxis(SplineMeshAxis, false);
		Component->SetSplineUpDir(UpVector, false);

		Component->SetStartOffset(Params.StartOffset, false);
		Component->SetEndOffset(Params.EndOffset, false);

		Component->SplineParams.NaniteClusterBoundsScale = Params.NaniteClusterBoundsScale;

		Component->SplineBoundaryMin = 0;
		Component->SplineBoundaryMax = 0;

		Component->bSmoothInterpRollScale = bSmoothInterpRollScale;
	}
}

bool FPCGExSplineMeshMutationDetails::Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (!bPushStart && !bPushEnd)
	{
		return true;
	}

	if (bPushStart)
	{
		StartAmount = StartPush.GetValueSetting();
		if (!StartAmount->Init(InDataFacade))
		{
			return false;
		}
	}

	if (bPushEnd)
	{
		EndAmount = EndPush.GetValueSetting();
		if (!EndAmount->Init(InDataFacade))
		{
			return false;
		}
	}

	return true;
}

void FPCGExSplineMeshMutationDetails::Mutate(const int32 PointIndex, PCGExPaths::FSplineMeshSegment& InSegment)
{
	if (!bPushStart && !bPushEnd)
	{
		return;
	}

	const double Size = (bPushStart || bPushEnd) && (bRelativeStart || bRelativeEnd) ? FVector::Dist(InSegment.Params.StartPos, InSegment.Params.EndPos) : 1;
	const FVector StartDir = InSegment.Params.StartTangent.GetSafeNormal();
	const FVector EndDir = InSegment.Params.EndTangent.GetSafeNormal();

	if (bPushStart)
	{
		const double Factor = StartAmount->Read(PointIndex);
		const double Dist = bRelativeStart ? Size * Factor : Factor;

		InSegment.Params.StartPos -= StartDir * Dist;
		InSegment.Params.StartTangent = StartDir * (InSegment.Params.StartTangent.Size() + Dist * 3);
	}

	if (bPushEnd)
	{
		const double Factor = EndAmount->Read(PointIndex);
		const double Dist = bRelativeEnd ? Size * Factor : Factor;

		InSegment.Params.EndPos += EndDir * Dist;
		InSegment.Params.EndTangent = EndDir * (InSegment.Params.EndTangent.Size() + Dist * 3);
	}
}

#if WITH_EDITOR
void FPCGExSplineMeshMutationDetails::ApplyDeprecation()
{
	StartPush.Update(StartPushInput_DEPRECATED, StartPushInputAttribute_DEPRECATED, StartPushConstant_DEPRECATED);
	EndPush.Update(EndPushInput_DEPRECATED, EndPushInputAttribute_DEPRECATED, EndPushConstant_DEPRECATED);
}

void FPCGExSplineMeshMutationDetails::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("StartPushInputAttribute")), FName(TEXT("StartPush")), FName(TEXT("Attribute")), FName(TEXT(" ├─ Amount (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("StartPushConstant")), FName(TEXT("StartPush")), FName(TEXT("Constant")), FName(TEXT(" ├─ Amount")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("EndPushInputAttribute")), FName(TEXT("EndPush")), FName(TEXT("Attribute")), FName(TEXT(" ├─ Amount (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("EndPushConstant")), FName(TEXT("EndPush")), FName(TEXT("Constant")), FName(TEXT(" ├─ Amount")));
}
#endif
