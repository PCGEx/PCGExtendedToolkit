// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExBoundsEvaluator.h"

#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

#pragma region UPCGExBoundsEvaluator

FBox UPCGExBoundsEvaluator::EvaluateActorBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	if (!Actor)
	{
		return FBox(ForceInit);
	}

	FBox AccumulatedBounds(ForceInit);

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (const UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp->IsRegistered())
		{
			continue;
		}
		AccumulatedBounds += PrimComp->Bounds.GetBox();
	}

	return AccumulatedBounds;
}

FBox UPCGExBoundsEvaluator::EvaluateActorLocalBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	if (!Actor)
	{
		return FBox(ForceInit);
	}

	FBox AccumulatedBounds(ForceInit);

	// Actor-frame accumulation: CalcBounds against the component's transform relative to the
	// actor, with actor scale divided out so callers compose it back via the actor transform.
	const FTransform ActorToWorld = Actor->GetActorTransform();

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (const UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp->IsRegistered())
		{
			continue;
		}
		AccumulatedBounds += PrimComp->CalcBounds(
			PrimComp->GetComponentTransform().GetRelativeTransform(ActorToWorld)).GetBox();
	}

	return AccumulatedBounds;
}

#pragma endregion

#pragma region UPCGExDefaultBoundsEvaluator

bool UPCGExDefaultBoundsEvaluator::QualifiesForBounds(const UPrimitiveComponent* PrimComp) const
{
	if (!PrimComp->IsRegistered())
	{
		return false;
	}
	if (bIgnoreEditorOnlyComponents && PrimComp->IsEditorOnly())
	{
		return false;
	}
	if (bOnlyCollidingComponents && !PrimComp->IsCollisionEnabled())
	{
		return false;
	}
	if (bIgnoreLightComponents && PrimComp->IsA<ULightComponent>())
	{
		return false;
	}
	return true;
}

FBox UPCGExDefaultBoundsEvaluator::EvaluateActorBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	if (!Actor)
	{
		return FBox(ForceInit);
	}

	FBox AccumulatedBounds(ForceInit);

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents, bIncludeFromChildActors);

	for (const UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!QualifiesForBounds(PrimComp))
		{
			continue;
		}
		AccumulatedBounds += PrimComp->Bounds.GetBox();
	}

	return AccumulatedBounds;
}

FBox UPCGExDefaultBoundsEvaluator::EvaluateActorLocalBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	if (!Actor)
	{
		return FBox(ForceInit);
	}

	FBox AccumulatedBounds(ForceInit);

	const FTransform ActorToWorld = Actor->GetActorTransform();

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents, bIncludeFromChildActors);

	for (const UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!QualifiesForBounds(PrimComp))
		{
			continue;
		}
		AccumulatedBounds += PrimComp->CalcBounds(
			PrimComp->GetComponentTransform().GetRelativeTransform(ActorToWorld)).GetBox();
	}

	return AccumulatedBounds;
}

#pragma endregion
