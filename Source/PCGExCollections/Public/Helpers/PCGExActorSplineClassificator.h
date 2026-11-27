// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExActorSplineClassificator.generated.h"

class AActor;

/**
 * Abstract base for actor spline classification, sibling of the mesh classificator.
 * Determines whether an actor is harvested for its splines: whatever PCG's Get Spline Data reads from its
 * components or from the actor itself. An actor may be both a mesh and a spline container.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, meta=(PCGExNodeLibraryDoc="staging/collections/helpers/actor-spline-classificator"))
class PCGEXCOLLECTIONS_API UPCGExActorSplineClassificator : public UObject
{
	GENERATED_BODY()

public:
	/** Also export the path of each spline mesh component, as a two-point spline. Applies to actors tagged
	 *  StaticClassifySpline too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	bool bConvertSplineMeshes = false;

	/** Returns true if this actor should be checked for spline content and harvested for its splines. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Classification")
	bool ShouldClassifyAsSpline(AActor* Actor) const;

	virtual bool ShouldClassifyAsSpline_Implementation(AActor* Actor) const;
};

/**
 * Default spline classificator driven by class and tag include/exclude lists.
 * An actor is a spline container if it matches at least one include rule and none of the exclude rules.
 * Every list starts empty: no actor is harvested for splines until one is listed or tagged StaticClassifySpline.
 */
UCLASS(DisplayName = "Default Actor Spline Classificator")
class PCGEXCOLLECTIONS_API UPCGExDefaultActorSplineClassificator : public UPCGExActorSplineClassificator
{
	GENERATED_BODY()

public:
	/** Actors of these classes (or subclasses) are harvested for their splines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<TSoftClassPtr<AActor>> IncludeClasses;

	/** Actors with any of these tags are harvested for their splines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<FName> IncludeTags;

	/** Actors of these classes (or subclasses) are never harvested for their splines, even if they match include rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<TSoftClassPtr<AActor>> ExcludeClasses;

	/** Actors with any of these tags are never harvested for their splines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	TArray<FName> ExcludeTags;

	virtual bool ShouldClassifyAsSpline_Implementation(AActor* Actor) const override;
};
