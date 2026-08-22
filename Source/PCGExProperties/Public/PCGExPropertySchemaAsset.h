// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"
#include "Engine/DataAsset.h"

#include "PCGExPropertySchemaAsset.generated.h"

class FDataValidationContext;
class UPCGExPropertySchemaAsset;

/**
 * Editor-time notification that a schema asset's contents changed.
 *
 * Subscribers (typically the collection customization) react by reconciling their
 * ImportOverrides and refreshing UI. The sender argument identifies the changed asset
 * so listeners that subscribe to multiple assets can act selectively.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGExSchemaAssetChanged, UPCGExPropertySchemaAsset* /*ChangedAsset*/);

/**
 * Reusable schema asset.
 *
 * Wraps an FPCGExPropertySchemaCollection so other collections can pull in a shared
 * set of property definitions through their own ImportedSchemas array. Because the
 * wrapped collection itself carries an ImportedSchemas array, composition is recursive.
 *
 * Resolution semantics (when imported into another collection):
 * - Name-keyed, first-wins. Locals in the importing collection beat imports;
 *   earlier entries in ImportedSchemas beat later ones.
 * - Walk order is depth-first: importing collection's locals first, then each
 *   ImportedSchemas entry expanded in array order (locals of the imported
 *   collection, then its own imports, etc.).
 * - Cycles are detected during resolution and skipped with a LogPCGEx warning.
 *   IsDataValid surfaces them in the editor as a Warning result.
 */

// Property Schema
UCLASS(BlueprintType, DisplayName="[PCGEx] Property Schema", meta=(ToolTip = "A composable and inheritable library of properties definition.", PCGExNodeLibraryDoc="staging/utilities/property-schema-asset"))
class PCGEXPROPERTIES_API UPCGExPropertySchemaAsset : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, MultiLine))
	FString Notes;
#endif

	/** Schemas (and further imports) carried by this asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Settings)
	FPCGExPropertySchemaCollection Collection;

#if WITH_EDITOR
	/**
	 * Fires from PostEditChangeProperty after the asset's local schemas have been re-synced.
	 * Editor-only: not registered or fired in cooked builds.
	 */
	FOnPCGExSchemaAssetChanged OnSchemaAssetChanged;

	/**
	 * Class-wide counterpart of OnSchemaAssetChanged, fired for EVERY schema asset edit.
	 * Host modules subscribe once here and filter with ImportsAssetTransitive -- unlike the
	 * per-instance delegate, no live details customization is needed for hosts to reconcile.
	 */
	static FOnPCGExSchemaAssetChanged OnAnySchemaAssetChanged;

	virtual void PostLoad() override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
