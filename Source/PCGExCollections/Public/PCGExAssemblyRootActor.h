// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGExAssemblyRoot.h"

#include "PCGExAssemblyRootActor.generated.h"

class UBillboardComponent;

/**
 * Barebone assembly root: whatever is attached under it is exported as if it were a level, relative to
 * this actor. Point a PCGDataAsset collection entry (Source == Actor) at it, or drop it inside a
 * Valency cage, which registers it as one DataAsset module. Authors nothing itself and spawns nothing.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "[PCGEx] Assembly Root", meta=(PCGExNodeLibraryDoc="staging/collections/pcg-data-asset-collection/assembly-root"))
class PCGEXCOLLECTIONS_API APCGExAssemblyRootActor : public AActor, public IPCGExAssemblyRoot
{
	GENERATED_BODY()

public:
	APCGExAssemblyRootActor();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
