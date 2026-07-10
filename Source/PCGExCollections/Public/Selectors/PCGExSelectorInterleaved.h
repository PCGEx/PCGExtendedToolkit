// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorInterleaved.generated.h"

/**
 * Selector-specific configuration for Interleaved. Shared verbatim between the palette node
 * settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for serialization).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorInterleavedConfig
{
	GENERATED_BODY()

	/**
	 * Per-point ordinal driving the low-discrepancy sequence. Defaults to the point index,
	 * which yields the classic golden-ratio interleave along point order. Any scalar attribute
	 * works -- distinct ordinals map to well-spread sequence positions.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExInputShorthandSelectorDouble OrdinalSource = FPCGExInputShorthandSelectorDouble(FName("$Index"), 0.0, true);

	/** Constant phase offset added to the sequence, in [0, 1]. Rotates which entries the sequence starts on. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, UIMin=0, UIMax=1))
	double Phase = 0.0;

	/** When enabled, entries occupy sequence bands proportional to their Weight; otherwise all entries get equal bands. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUseWeights = true;
};

/**
 * Collection-derived state for Interleaved: cumulative entry weights, built once per
 * (Factory, Category) and reused across facades via FSelectorSharedDataCache.
 */
class FPCGExInterleavedSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<double> Cumulative; // parallel to Target->Entries
	double TotalWeight = 0.0;
};

/**
 * Low-discrepancy weighted pick: t = Frac((Ordinal + 0.5) * φ⁻¹ + Phase) mapped through the
 * cumulative weight bands. Respects weight proportions like WeightedRandom but with maximally
 * even interleaving -- no clumps of identical picks.
 *
 * DETERMINISTIC BY ORDINAL: the per-point Seed is intentionally ignored (randomizing would
 * reintroduce the clumping this selector exists to remove). LocalSeed still de-correlates
 * multiple Interleaved selectors by rotating the phase (applied at factory time).
 */
class FPCGExEntryInterleavedPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExInterleavedSharedData>
{
public:
	// Copied from factory before PrepareForData. EffectivePhase = Config.Phase + LocalSeed rotation.
	double EffectivePhase = 0.0;
	bool bUseWeights = true;
	FPCGExInputShorthandSelectorDouble OrdinalSource;

	TSharedPtr<PCGExDetails::TSettingValue<double>> OrdinalGetter;

	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

protected:
	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;
};

/**
 * Factory data for Interleaved selection.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorInterleavedFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorInterleavedConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector : Interleaved".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/staging-distribute/selector-interleaved"))
class PCGEXCOLLECTIONS_API UPCGExSelectorInterleavedFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorInterleaved, "Selector : Interleaved",
		"Low-discrepancy weighted pick (golden-ratio sequence). Respects weight proportions with maximally even interleaving -- no clumps. Deterministic by ordinal; ignores the per-point seed.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorInterleavedConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
