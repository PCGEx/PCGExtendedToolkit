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

	double GetAngle(const EPCGExAngleRange Mode, const FVector& A, const FVector& B, const FVector& Up)
	{
		const FVector N1 = A.GetSafeNormal();
		const FVector N2 = B.GetSafeNormal();
		const FVector C = FVector::CrossProduct(N1, N2);

		// Atan2 form is immune to FP rounding pushing an Acos input outside [-1, 1]
		return MapAngle(Mode, FMath::Atan2(C.Size(), N1.Dot(N2)), C.Dot(Up) < 0);
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
