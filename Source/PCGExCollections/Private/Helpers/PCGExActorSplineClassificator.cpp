// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorSplineClassificator.h"

#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorClassificationRules.h"

#pragma region UPCGExActorSplineClassificator

bool UPCGExActorSplineClassificator::ShouldClassifyAsSpline_Implementation(AActor* Actor) const
{
	return false;
}

#pragma endregion

#pragma region UPCGExDefaultActorSplineClassificator

bool UPCGExDefaultActorSplineClassificator::ShouldClassifyAsSpline_Implementation(AActor* Actor) const
{
	return PCGExActorClassificationRules::Matches(Actor, IncludeClasses, IncludeTags, ExcludeClasses, ExcludeTags);
}

#pragma endregion
