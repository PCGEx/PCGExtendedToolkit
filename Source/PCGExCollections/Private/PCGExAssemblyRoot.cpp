// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExAssemblyRoot.h"

#include "GameFramework/Actor.h"

FTransform IPCGExAssemblyRoot::GetAssemblyFrame() const
{
	const AActor* Self = Cast<AActor>(this);
	return Self ? Self->GetActorTransform() : FTransform::Identity;
}
