// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Selectors/PCGExSelectorTagFilter.h"

#include "PCGExBuildVariantCollection.generated.h"

/**
 * Builds a transient Variant collection holding one material swap per input row, and outputs its soft path for
 * Staging : Swap's Additional Variant override pin. Lifetime and reuse follow Build Asset Collection: anchored by a
 * managed resource on the component, reused while the inputs and settings are unchanged. Main-thread-only + non-cacheable.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Collections", meta = (PCGExNodeLibraryDoc = "staging/utilities/build-variant-collection"))
class UPCGExBuildVariantCollectionSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildVariantCollection, "Build Variant Collection", "Builds a transient Variant collection from input attribute sets (one material swap per row) and outputs its soft path for Staging : Swap's Additional Variant override.")

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }

	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Misc); }
#endif

	// Non-cacheable so the reuse/mark-used pass runs every generation (via ShouldCache).
	virtual bool IsCacheable() const override { return false; }

	// Tag the output as a soft object path so it connects to soft-path override pins (Staging : Swap's Additional Variant).
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Attribute holding the material a row replaces (Soft Object Path or String). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName MatchAttribute = FName("MatchMaterial");

	/** Attribute holding the material rendered instead (Soft Object Path or String). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName ReplacementAttribute = FName("Replacement");

	/** If enabled, gate every built swap on the staged entry's tags. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bUseTagFilter = false;

	/** When enabled, a built swap only applies to entries whose tags pass. Each clause is a constant or a per-row Name / String
	 *  attribute; a material's rows are tried in order, the first passing wins. Collection reads the entry's collection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bUseTagFilter"))
	FPCGExBaseTagFilterDetails TagFilter;

	/** Name of the output Soft Object Path attribute carrying the built variant's soft path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName OutputAttributeName = FName("Variant");

	/** Suppress the warning about skipped rows (an empty path, or a replacement that is not a material). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuietSkippedRowsWarning = false;
};

struct FPCGExBuildVariantCollectionContext final : FPCGExContext
{
};

class FPCGExBuildVariantCollectionElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildVariantCollection)

	// GT-only: NewObject + managed resource registration.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
