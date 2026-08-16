// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExProperty.h"

#include "PCGExCategoryOverrides.generated.h"

/**
 * Authored per-category data on a collection, one row per category name that carries overrides.
 * Resolves between an entry's own authoring and the collection's defaults. NAME_None is never a
 * key: an uncategorized entry contributes no layer.
 *
 * A row may only refine names the collection schema already declares -- SyncToSchema enforces
 * that structurally, which is what keeps every prototype/layout scan correct without a tier.
 * Members may be appended, never reordered or renamed. Must never gain a UPROPERTY(Instanced)
 * member: rows are value-copied across assets by the Omni merge, which would share the subobject.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Category Overrides")
struct PCGEXCOLLECTIONS_API FPCGExCategoryOverrides
{
	GENERATED_BODY()

	/** Category this row refines. Rewritten only by EDITOR_RenameCategoryOverrides. */
	UPROPERTY(VisibleAnywhere, Category = Settings)
	FName Category = NAME_None;

	/** Schema-parallel, exactly like an entry's -- same SyncToSchema / ApplyHeaderIdRemap contract. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(NoResetToDefault))
	FPCGExPropertyOverrides PropertyOverrides;

	FPCGExCategoryOverrides() = default;

	explicit FPCGExCategoryOverrides(const FName InCategory)
		: Category(InCategory)
	{
	}

	/** True when at least one slot is enabled -- cache this for UI, it is O(N) over the schema. */
	bool HasAnyEnabled() const
	{
		return PropertyOverrides.GetEnabledCount() > 0;
	}
};
