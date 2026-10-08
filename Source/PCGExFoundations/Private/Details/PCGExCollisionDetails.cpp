// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExCollisionDetails.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PCGComponent.h"
#include "PCGExLog.h"
#include "PCGNode.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExInputShorthandsDetails.h"

FPCGExCollisionDetails::FPCGExCollisionDetails()
{
	// An ignore list: self and PCG-spawned actors are as ignorable as any other, and self has its own switch.
	IgnoredActors.bIgnoreSelf = false;
	IgnoredActors.bIgnorePCGSpawnedActors = false;
}

#if WITH_EDITOR
void FPCGExCollisionDetails::ApplyDeprecation(const UObject* InLogContext)
{
	IgnoredActors.ApplyDeprecation(IgnoredActorSelector_DEPRECATED, InLogContext);

	if (bIgnoreActors && IgnoredActors.bMustOverlapSelf)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("%s: Must Overlap Self on the ignored actors had no effect before and now limits the ignore list to actors overlapping the component bounds."), *GetPathNameSafe(InLogContext));
	}
}

void FPCGExCollisionDetails::MigrateLegacyPins(const UPCGSettings* InSettings, UPCGNode* InOutNode, TArray<FName>& OutRetiredLabels)
{
	if (!InSettings || !InOutNode) { return; }

	// A wired tag could carry a pattern, which stock matched: the clause must keep matching it.
	if (!PCGExDeprecation::ResolveLegacyOverridePinLabel(InOutNode, TEXT("ActorSelectionTag")).IsNone())
	{
		IgnoredActors.bAllowWildcards = true;
	}

	// Same type on both sides, or a PCG broadcast (FName tag -> FString clause): the wire keeps working.
	const FName Member = TEXT("IgnoredActors");
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("ActorFilter"), Member, TEXT("Scope"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("bIncludeChildren"), Member, TEXT("bIncludeChildren"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("ActorSelectionTag"), Member, TEXT("RequireAny"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("ActorSelectionClass"), Member, TEXT("ActorClass"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("bIgnoreSelfAndChildren"), Member, TEXT("bIgnoreSelf"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, TEXT("bMustOverlapSelf"), Member, TEXT("bMustOverlapSelf"));

	// Nothing can receive these: the ByTag/ByClass mode is fixed by the property migration, the rest were dropped.
	for (const TCHAR* Leaf : {TEXT("ActorSelection"), TEXT("bDisableFilter"), TEXT("bSelectMultiple"), TEXT("IncludeIsolatedActors"), TEXT("ActorReferenceSelector")})
	{
		const FName Label = PCGExDeprecation::ResolveLegacyOverridePinLabel(InOutNode, FName(Leaf));
		if (!Label.IsNone())
		{
			OutRetiredLabels.AddUnique(Label);
		}
	}
}
#endif

void FPCGExCollisionDetails::Init(FPCGExContext* InContext)
{
	PCGExMT::ExecuteOnMainThreadAndWait([CtxHandle = InContext->GetWeakSelfHandle(), this]
	{
		PCGEX_SHARED_CONTEXT_VOID(CtxHandle);
		World = SharedContext.Get()->GetWorld();

		const UPCGComponent* Comp = SharedContext.Get()->GetComponent();

		if (bIgnoreActors)
		{
			IgnoredActors.Init();

			PCGExActorSelection::FQuery Query;
			if (PCGExActorSelection::FQuery::Make(IgnoredActors, SharedContext.Get(), Query))
			{
				Query.Run([this](AActor* InActor) { IgnoredActorList.Add(InActor); });
			}
		}

		if (bIgnoreSelf)
		{
			IgnoredActorList.Add(Comp->GetOwner());
		}
	});
}

void FPCGExCollisionDetails::Update(FCollisionQueryParams& InCollisionParams) const
{
	InCollisionParams.bTraceComplex = bTraceComplex;
	InCollisionParams.AddIgnoredActors(IgnoredActorList);
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
	return Linecast(From, To, HitResult);
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
