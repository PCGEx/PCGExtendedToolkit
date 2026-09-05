// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExCollisionDetails.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PCGComponent.h"
#include "Core/PCGExContext.h"

void FPCGExCollisionDetails::Init(FPCGExContext* InContext)
{
	PCGExMT::ExecuteOnMainThreadAndWait([CtxHandle = InContext->GetWeakSelfHandle(), this]
	{
		PCGEX_SHARED_CONTEXT_VOID(CtxHandle);
		World = SharedContext.Get()->GetWorld();

		const UPCGComponent* Comp = SharedContext.Get()->GetComponent();

		if (bIgnoreActors)
		{
			const TFunction<bool(const AActor*)> BoundsCheck = [](const AActor*) -> bool
			{
				return true;
			};
			const TFunction<bool(const AActor*)> SelfIgnoreCheck = [](const AActor*) -> bool
			{
				return true;
			};

			IgnoredActors = PCGActorSelector::FindActors(IgnoredActorSelector, Comp, BoundsCheck, SelfIgnoreCheck);
		}

		if (bIgnoreSelf)
		{
			IgnoredActors.Add(Comp->GetOwner());
		}
	});
}

void FPCGExCollisionDetails::Update(FCollisionQueryParams& InCollisionParams) const
{
	InCollisionParams.bTraceComplex = bTraceComplex;
	InCollisionParams.AddIgnoredActors(IgnoredActors);
}

bool FPCGExCollisionDetails::Linecast(const FVector& From, const FVector& To, FHitResult& HitResult) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->LineTraceSingleByChannel(HitResult, From, To, CollisionChannel, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->LineTraceSingleByObjectType(HitResult, From, To, FCollisionObjectQueryParams(CollisionObjectType), CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->LineTraceSingleByProfile(HitResult, From, To, CollisionProfileName, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::Linecast(const FVector& From, const FVector& To) const
{
	FHitResult HitResult;
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->LineTraceSingleByChannel(HitResult, From, To, CollisionChannel, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->LineTraceSingleByObjectType(HitResult, From, To, FCollisionObjectQueryParams(CollisionObjectType), CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->LineTraceSingleByProfile(HitResult, From, To, CollisionProfileName, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::StrongLinecast(const FVector& From, const FVector& To) const
{
	FHitResult HitResult;
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
	{
		if (!World->LineTraceSingleByChannel(HitResult, From, To, CollisionChannel, CollisionParams))
		{
			return World->LineTraceSingleByChannel(HitResult, To, From, CollisionChannel, CollisionParams);
		}
		return true;
	}
	case EPCGExCollisionFilterType::ObjectType:
	{
		if (!World->LineTraceSingleByObjectType(HitResult, From, To, FCollisionObjectQueryParams(CollisionObjectType), CollisionParams))
		{
			return World->LineTraceSingleByObjectType(HitResult, To, From, FCollisionObjectQueryParams(CollisionObjectType), CollisionParams);
		}
		return true;
	}
	case EPCGExCollisionFilterType::Profile:
	{
		if (!World->LineTraceSingleByProfile(HitResult, From, To, CollisionProfileName, CollisionParams))
		{
			return World->LineTraceSingleByProfile(HitResult, To, From, CollisionProfileName, CollisionParams);
		}
		return true;
	}
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::Linecast(const FVector& From, const FVector& To, bool bStrong) const
{
	if (bStrong)
	{
		return StrongLinecast(From, To);
	}
	return Linecast(From, To);
}

bool FPCGExCollisionDetails::LinecastMulti(const FVector& From, const FVector& To, TArray<FHitResult>& OutHits) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->LineTraceMultiByChannel(OutHits, From, To, CollisionChannel, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->LineTraceMultiByObjectType(OutHits, From, To, FCollisionObjectQueryParams(CollisionObjectType), CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->LineTraceMultiByProfile(OutHits, From, To, CollisionProfileName, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::SphereSweep(const FVector& From, const FVector& To, const double Radius, FHitResult& HitResult, const FQuat& Orientation) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->SweepSingleByChannel(HitResult, From, To, Orientation, CollisionChannel, Shape, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->SweepSingleByObjectType(HitResult, From, To, Orientation, FCollisionObjectQueryParams(CollisionObjectType), Shape, CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->SweepSingleByProfile(HitResult, From, To, Orientation, CollisionProfileName, Shape, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::SphereSweep(const FVector& From, const FVector& To, const double Radius, const FQuat& Orientation) const
{
	FHitResult HitResult;
	return SphereSweep(From, To, Radius, HitResult, Orientation);
}

bool FPCGExCollisionDetails::SphereSweepMulti(const FVector& From, const FVector& To, const double Radius, TArray<FHitResult>& OutHits, const FQuat& Orientation) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->SweepMultiByChannel(OutHits, From, To, Orientation, CollisionChannel, Shape, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->SweepMultiByObjectType(OutHits, From, To, Orientation, FCollisionObjectQueryParams(CollisionObjectType), Shape, CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->SweepMultiByProfile(OutHits, From, To, Orientation, CollisionProfileName, Shape, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::BoxSweep(const FVector& From, const FVector& To, const FVector& HalfExtents, FHitResult& HitResult, const FQuat& Orientation) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->SweepSingleByChannel(HitResult, From, To, Orientation, CollisionChannel, Shape, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->SweepSingleByObjectType(HitResult, From, To, Orientation, FCollisionObjectQueryParams(CollisionObjectType), Shape, CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->SweepSingleByProfile(HitResult, From, To, Orientation, CollisionProfileName, Shape, CollisionParams);
	default:
		return false;
	}
}

bool FPCGExCollisionDetails::BoxSweep(const FVector& From, const FVector& To, const FVector& HalfExtents, const FQuat& Orientation) const
{
	FHitResult HitResult;
	return BoxSweep(From, To, HalfExtents, HitResult, Orientation);
}

bool FPCGExCollisionDetails::BoxSweepMulti(const FVector& From, const FVector& To, const FVector& HalfExtents, TArray<FHitResult>& OutHits, const FQuat& Orientation) const
{
	FCollisionQueryParams CollisionParams;
	Update(CollisionParams);

	const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);

	switch (CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		return World->SweepMultiByChannel(OutHits, From, To, Orientation, CollisionChannel, Shape, CollisionParams);
	case EPCGExCollisionFilterType::ObjectType:
		return World->SweepMultiByObjectType(OutHits, From, To, Orientation, FCollisionObjectQueryParams(CollisionObjectType), Shape, CollisionParams);
	case EPCGExCollisionFilterType::Profile:
		return World->SweepMultiByProfile(OutHits, From, To, Orientation, CollisionProfileName, Shape, CollisionParams);
	default:
		return false;
	}
}
