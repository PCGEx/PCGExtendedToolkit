// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExSamplingHelpers.h"

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGModule.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"

#include "GameFramework/Actor.h"
#include "Sampling/PCGExSamplingCommon.h"

namespace PCGExSampling::Helpers
{
	double MapAngle(const EPCGExAngleRange Mode, const double Radians, const bool bFlipWinding)
	{
		const double Degrees = FMath::RadiansToDegrees(Radians); // 0 .. 180

		switch (Mode)
		{
		default:
		case EPCGExAngleRange::URadians: // 0 .. PI
			return Radians;
		case EPCGExAngleRange::PIRadians: // -PI .. PI
			return bFlipWinding ? -Radians : Radians;
		case EPCGExAngleRange::TAURadians: // 0 .. TAU
			return bFlipWinding ? TWO_PI - Radians : Radians;
		case EPCGExAngleRange::UDegrees: // 0 .. 180
			return Degrees;
		case EPCGExAngleRange::PIDegrees: // -180 .. 180
			return bFlipWinding ? -Degrees : Degrees;
		case EPCGExAngleRange::TAUDegrees: // 0 .. 360
			return bFlipWinding ? 360 - Degrees : Degrees;
		case EPCGExAngleRange::NormalizedHalf: // 0 .. 180 -> 0 .. 1
			return Degrees / 180;
		case EPCGExAngleRange::Normalized: // 0 .. 360 -> 0 .. 1
			return (bFlipWinding ? 360 - Degrees : Degrees) / 360;
		case EPCGExAngleRange::InvertedNormalizedHalf: // 0 .. 180 -> 1 .. 0
			return 1 - (Degrees / 180);
		case EPCGExAngleRange::InvertedNormalized: // 0 .. 360 -> 1 .. 0
			return 1 - ((bFlipWinding ? 360 - Degrees : Degrees) / 360);
		}
	}

	double GetAngle(const EPCGExAngleRange Mode, const FVector& A, const FVector& B)
	{
		double OutAngle = 0;

		const FVector N1 = A.GetSafeNormal();
		const FVector N2 = B.GetSafeNormal();

		const double MainDot = N1.Dot(N2);
		const FVector C = FVector::CrossProduct(N1, N2);

		switch (Mode)
		{
		case EPCGExAngleRange::URadians: // 0 .. 3.14
			OutAngle = FMath::Acos(MainDot);
			break;
		case EPCGExAngleRange::PIRadians: // -3.14 .. 3.14
			OutAngle = FMath::Acos(MainDot) * FMath::Sign(MainDot);
			break;
		case EPCGExAngleRange::TAURadians: // 0 .. 6.28
			if (C.Z < 0)
			{
				OutAngle = TWO_PI - FMath::Atan2(C.Size(), MainDot);
			}
			else
			{
				OutAngle = FMath::Atan2(C.Size(), MainDot);
			}
			break;
		case EPCGExAngleRange::UDegrees: // 0 .. 180
			OutAngle = FMath::RadiansToDegrees(FMath::Acos(MainDot));
			break;
		case EPCGExAngleRange::PIDegrees: // -180 .. 180
			OutAngle = FMath::RadiansToDegrees(FMath::Acos(MainDot)) * FMath::Sign(MainDot);
			break;
		case EPCGExAngleRange::TAUDegrees: // 0 .. 360
			if (C.Z < 0)
			{
				OutAngle = 360 - FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			else
			{
				OutAngle = FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			break;
		case EPCGExAngleRange::NormalizedHalf: // 0 .. 180 -> 0 .. 1
			OutAngle = FMath::RadiansToDegrees(FMath::Acos(MainDot)) / 180;
			break;
		case EPCGExAngleRange::Normalized: // 0 .. 360 -> 0 .. 1
			if (C.Z < 0)
			{
				OutAngle = 360 - FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			else
			{
				OutAngle = FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			OutAngle /= 360;
			break;
		case EPCGExAngleRange::InvertedNormalizedHalf: // 0 .. 180 -> 1 .. 0
			OutAngle = 1 - (FMath::RadiansToDegrees(FMath::Acos(MainDot)) / 180);
			break;
		case EPCGExAngleRange::InvertedNormalized: // 0 .. 360 -> 1 .. 0
			if (C.Z < 0)
			{
				OutAngle = 360 - FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			else
			{
				OutAngle = FMath::RadiansToDegrees(FMath::Atan2(C.Size(), MainDot));
			}
			OutAngle /= 360;
			OutAngle = 1 - OutAngle;
			break;
		default: ;
		}

		return OutAngle;
	}

	bool GetIncludedActors(const FPCGContext* InContext, const TSharedRef<PCGExData::FFacade>& InFacade, const FName ActorReferenceName, TMap<AActor*, int32>& OutActorSet)
	{
		FPCGAttributePropertyInputSelector Selector = FPCGAttributePropertyInputSelector();
		Selector.SetAttributeName(ActorReferenceName);

		const TUniquePtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> ActorReferences = MakeUnique<PCGExData::TAttributeBroadcaster<FSoftObjectPath>>();
		if (!ActorReferences->Prepare(Selector, InFacade->Source))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Actor reference attribute does not exist."));
			return false;
		}

		ActorReferences->Grab();

		for (int i = 0; i < ActorReferences->Values.Num(); i++)
		{
			const FSoftObjectPath& Path = ActorReferences->Values[i];
			if (!Path.IsValid())
			{
				continue;
			}
			if (AActor* TargetActor = Cast<AActor>(Path.ResolveObject()))
			{
				OutActorSet.FindOrAdd(TargetActor, i);
			}
		}

		return true;
	}
}
