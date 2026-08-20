// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExActorMeshClassificator.generated.h"

class AActor;

/**
 * Abstract base for actor mesh classification.
 * Instanced on exporters via EditInlineNew/DefaultToInstanced.
 * Determines whether an actor should be treated as a mesh container
 * (parsed for UStaticMeshComponent / UInstancedStaticMeshComponent).
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, meta=(PCGExNodeLibraryDoc="staging/collections/helpers/actor-mesh-classificator"))
class PCGEXCOLLECTIONS_API UPCGExActorMeshClassificator : public UObject
{
	GENERATED_BODY()

public:
	/** Returns true if this actor should be checked for mesh components
	 *  and classified as a mesh point rather than an actor reference. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Classification")
	bool ShouldClassifyAsMesh(AActor* Actor) const;

	virtual bool ShouldClassifyAsMesh_Implementation(AActor* Actor) const;
};

/**
 * Default mesh classificator driven by class and tag include/exclude lists.
 * An actor is treated as a mesh container if it matches at least one include rule
 * and none of the exclude rules. Defaults to AStaticMeshActor as the sole include class.
 */
UCLASS(DisplayName = "Default Actor Mesh Classificator")
class PCGEXCOLLECTIONS_API UPCGExDefaultActorMeshClassificator : public UPCGExActorMeshClassificator
{
	GENERATED_BODY()

public:
	UPCGExDefaultActorMeshClassificator();

	/** Actors of these classes (or subclasses) are treated as mesh containers and will
	 *  be parsed for static/instanced mesh components. Defaults to AStaticMeshActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<TSoftClassPtr<AActor>> IncludeClasses;

	/** Actors with any of these tags are treated as mesh containers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<FName> IncludeTags;

	/** Actors of these classes (or subclasses) are never treated as mesh containers,
	 *  even if they match include rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<TSoftClassPtr<AActor>> ExcludeClasses;

	/** Actors with any of these tags are never treated as mesh containers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<FName> ExcludeTags;

	virtual bool ShouldClassifyAsMesh_Implementation(AActor* Actor) const override;
};
