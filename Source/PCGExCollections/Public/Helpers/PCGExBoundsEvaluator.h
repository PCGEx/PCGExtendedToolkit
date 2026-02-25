// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExBoundsEvaluator.generated.h"

class AActor;
class UPCGExAssetCollection;

/**
 * Abstract base for actor bounds evaluation.
 * Instanced on collections/exporters via EditInlineNew/DefaultToInstanced.
 * Returns world-space FBox from qualifying components.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class PCGEXCOLLECTIONS_API UPCGExBoundsEvaluator : public UObject
{
	GENERATED_BODY()

public:
	/** Returns world-space FBox from qualifying components. Invalid FBox = no contributions.
	 *  OwningCollection + EntryIndex provide optional context. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Bounds")
	FBox EvaluateActorBounds(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const;

	virtual FBox EvaluateActorBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const;
};

/**
 * Default bounds evaluator with component filtering options.
 */
UCLASS(DisplayName = "Default Bounds Evaluator")
class PCGEXCOLLECTIONS_API UPCGExDefaultBoundsEvaluator : public UPCGExBoundsEvaluator
{
	GENERATED_BODY()

public:
	/** When true, only components with collision enabled contribute to bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	bool bOnlyCollidingComponents = false;

	/** When true, light components are excluded from bounds computation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	bool bIgnoreLightComponents = true;

	/** When true, child actor components are included in bounds computation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	bool bIncludeFromChildActors = false;

	virtual FBox EvaluateActorBounds_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const override;
};
