// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"

#include "PCGExManagedActors.generated.h"

/** Managed actors found again by slot (the base CRC, see FPCGExContext::GetExecutionCrc), whether or not what they were spawned from still holds. */
UCLASS()
class PCGEXCORE_API UPCGExManagedActors : public UPCGManagedActors
{
	GENERATED_BODY()

public:
	/** Hash of what the actors were spawned from. Invalid while they are being spawned, so an interrupted run never passes for a complete one. */
	UPROPERTY()
	FPCGCrc ContentCrc;
};
