// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

namespace PCGExData
{
	class FFacade;

	template <typename T>
	class TBuffer;
}

class AActor;

struct FPCGContext;
enum class EPCGExAngleRange : uint8;

namespace PCGExSampling::Helpers
{
	/** Maps an unsigned angle (radians, 0..PI) onto the requested range; bFlipWinding selects the negative/reflex side of winding-aware ranges. */
	PCGEXBLENDING_API
	double MapAngle(const EPCGExAngleRange Mode, const double Radians, const bool bFlipWinding);

	/** Angle between A and B mapped to the requested range. Ranges that carry winding (PI/TAU/Normalized) resolve their sign against Up. */
	PCGEXBLENDING_API
	double GetAngle(const EPCGExAngleRange Mode, const FVector& A, const FVector& B, const FVector& Up = FVector::UpVector);

	PCGEXBLENDING_API
	bool GetIncludedActors(const FPCGContext* InContext, const TSharedRef<PCGExData::FFacade>& InFacade, const FName ActorReferenceName, TMap<AActor*, int32>& OutActorSet);

	/** Normalized-distance pass shared by the samplers. MaxDistance <= 0 leaves values untouched; SkipMask entries at 0 are left as written. */
	PCGEXBLENDING_API
	void NormalizeDistances(const TSharedPtr<PCGExData::TBuffer<double>>& Writer, const int32 NumPoints, const TArray<int8>* SkipMask, const double MaxDistance, const bool bOneMinus, const double Scale);

	PCGEXBLENDING_API
	void ApplySuccessTags(const TSharedRef<PCGExData::FFacade>& InFacade, const bool bAnySuccess, const bool bTagIfHasSuccesses, const FString& HasSuccessesTag, const bool bTagIfHasNoSuccesses, const FString& HasNoSuccessesTag);
}
