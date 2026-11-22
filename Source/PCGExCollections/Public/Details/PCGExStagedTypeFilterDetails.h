// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollectionTypes.h"

#include "PCGExStagedTypeFilterDetails.generated.h"

/**
 * Collection type filter configuration.
 * Automatically populated from the type registry.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExStagedTypeFilterDetails
{
	GENERATED_BODY()

	FPCGExStagedTypeFilterDetails();

	/** Type inclusion map - keys are read-only, populated from registry */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ReadOnlyKeys))
	TMap<FName, bool> TypeFilter;

	/** Include/exclude invalid/unresolved entries */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIncludeInvalid = false;

	/** Refresh type filter from registry (editor utility) */
	void RefreshFromRegistry();

	/** Check if a type ID matches the filter configuration */
	bool Matches(PCGExAssetCollection::FTypeId TypeId) const;

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
};
