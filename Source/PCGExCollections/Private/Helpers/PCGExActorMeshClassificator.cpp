// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorMeshClassificator.h"

#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorClassificationRules.h"

#pragma region UPCGExActorMeshClassificator

bool UPCGExActorMeshClassificator::ShouldClassifyAsMesh_Implementation(AActor* Actor) const
{
	return false;
}

#pragma endregion

#pragma region UPCGExDefaultActorMeshClassificator

UPCGExDefaultActorMeshClassificator::UPCGExDefaultActorMeshClassificator()
{
	IncludeClasses.Add(TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Script/Engine.StaticMeshActor"))));
}

bool UPCGExDefaultActorMeshClassificator::ShouldClassifyAsMesh_Implementation(AActor* Actor) const
{
	return PCGExActorClassificationRules::Matches(Actor, IncludeClasses, IncludeTags, ExcludeClasses, ExcludeTags);
}

#pragma endregion
