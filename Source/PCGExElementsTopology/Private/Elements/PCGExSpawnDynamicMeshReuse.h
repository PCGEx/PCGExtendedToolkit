// Copyright 2026 Eidos Interactive Corp and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Component reuse for the Spawn Dynamic Mesh node. Kept in its own file pair so that PCGExSpawnDynamicMesh.cpp only
// carries the two call sites.
//
// Without it the element creates a brand new component on every generation, named with MakeUniqueObjectName. A
// destroyed component keeps its name until the next garbage collection, so each replacement is given the next free one
// (PCGDynamicMeshComponent_0 -> _44 -> _88). That rewrites the owning actor's package on every World Partition bake
// even when nothing changed, which churns the actors in source control.

#pragma once

#include "CoreMinimal.h"

#include "PCGCrc.h"
#include "PCGManagedResource.h"

#include "PCGExSpawnDynamicMeshReuse.generated.h"

class AActor;
class IPCGElement;
class UPCGComponent;
class UPCGExDynamicMeshComponent;
class UPCGSettings;
struct FPCGExContext;
struct FPCGTaggedData;

/**
* Managed resource for a dynamic mesh component that may survive a regeneration.
*
* UPCGManagedComponent does not support reset, so PCG destroys the component during the cleanup that precedes a
* generation. Opting into reset makes the cleanup a soft release instead: the component is kept, marked unused, and
* released for good at the end of the generation if nobody claimed it.
*/
UCLASS()
class UPCGExManagedDynamicMeshComponent : public UPCGManagedComponent
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedComponentBase interface
	virtual bool SupportsComponentReset() const override { return bSupportsReset; }
	virtual void ResetComponent() override;
	//~End UPCGManagedComponentBase interface

	/**
	* Controls whether the component survives a regeneration so that the element which created it can pick it up again.
	* Stored on the resource rather than read from the console variable, so that toggling the variable never leaves a
	* resource that was created under one behaviour being released under the other.
	*/
	UPROPERTY()
	bool bSupportsReset = false;
};

namespace PCGExSpawnDynamicMesh
{
	/** The managed resource class to attach a freshly created component with, or nullptr to keep the default one. */
	UClass* GetManagedComponentClass();

	/**
	* The Crc identifying one input of the node, or an invalid Crc when it could not be computed, which simply disables
	* reuse for that input.
	*/
	FPCGCrc ComputeInputCrc(
		const IPCGElement* InElement,
		FPCGExContext* InContext,
		const UPCGSettings* InSettings,
		const FPCGTaggedData& InInput);

	/**
	* Picks up the component a previous generation created for this input, when nothing that input depends on has
	* changed. Returns nullptr when the caller has to create a fresh one.
	*/
	UPCGExDynamicMeshComponent* TryReuseComponent(
		UPCGComponent* InSourceComponent,
		const AActor* InTargetActor,
		const FPCGCrc& InInputCrc);

	/**
	* Stamps the resource backing a freshly created component with the Crc of the input it was built from, so that a
	* later generation can recognize it in TryReuseComponent.
	*/
	void EnableReuse(UPCGManagedComponent* InResource, const FPCGCrc& InInputCrc);
}
