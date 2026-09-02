// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExAssemblyRootActor.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"

APCGExAssemblyRootActor::APCGExAssemblyRootActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	// Its own components never render or collide; the attached content does. Deliberately NOT an
	// editor-only actor: the scanners that consume it reject editor-only actors.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Root);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}
