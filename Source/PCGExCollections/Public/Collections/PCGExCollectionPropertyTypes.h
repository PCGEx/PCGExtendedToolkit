// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"

#include "PCGExCollectionPropertyTypes.generated.h"

class UPCGExAssetCollection;

/**
 * Property referencing a PCGExAssetCollection.
 * Used for mesh/actor swapping based on pattern matches.
 * Does not support direct attribute output.
 */
USTRUCT(BlueprintType, DisplayName="Asset Collection")
struct PCGEXCOLLECTIONS_API FPCGExProperty_AssetCollection : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property")
	TSoftObjectPtr<UPCGExAssetCollection> AssetCollection;

	virtual FName GetTypeName() const override { return FName("AssetCollection"); }
};
