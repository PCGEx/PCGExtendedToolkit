// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExSelectorCurveRemapped.generated.h"

/**
 * Selector-specific configuration for Curve-Remapped Weight. Shared verbatim between the
 * palette node settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for
 * serialization).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorCurveRemappedConfig
{
	GENERATED_BODY()

	/** Name of the collection property (Float Curve type) supplying each entry's response curve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName CurvePropertyName = NAME_None;

	/** Per-point time value the curves are sampled at. Out-of-range values follow the curve's own extrapolation (clamped for LUT mode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExInputShorthandSelectorDouble TimeSource = FPCGExInputShorthandSelectorDouble(FName("$Density"), 1.0, true);

	/** If enabled, the curve response is multiplied by the entry's authored Weight; otherwise the curve alone drives probability. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bMultiplyByEntryWeight = true;

	/** Curve evaluation strategy (direct rich-curve eval vs baked lookup table). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, ShowOnlyInnerProperties))
	FPCGExCurveLookupDetails CurveLookup = FPCGExCurveLookupDetails(EPCGExCurveLUTMode::Lookup, 64);
};

/**
 * Collection-derived state for Curve-Remapped Weight: one baked curve lookup per entry that
 * resolves the configured Float Curve property (entry override first, collection default
 * second). Entries without the property are excluded. Built once per (Factory, Category).
 */
class FPCGExCurveRemappedSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<PCGExFloatLUT> EntryLookups; // parallel to Target->Entries; null where unresolved
	TArray<double> EntryWeights;        // (Weight + 1), parallel to Target->Entries
	TArray<int32> ValidEntryIndices;    // entries with a resolved curve
};

/**
 * Per-scope scratch for Curve-Remapped picks: reusable effective-weight buffer.
 */
class FPCGExCurveRemappedScratch : public FPCGExPickerScratchBase
{
public:
	TArray<double, TInlineAllocator<32>> EffectiveWeights;
};

/**
 * Curve-remapped weighted pick: per-point t drives each entry's weight through that entry's
 * authored response curve -- weight_i(t) = max(0, Curve_i(t)) [* Weight_i]. Entries whose
 * curve evaluates to <= 0 at t drop out of the pool entirely, so curves double as
 * range gates (altitude bands, moisture ranges, wear gradients...).
 */
class PCGEXCOLLECTIONS_API FPCGExEntryCurveRemappedPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExCurveRemappedSharedData>
{
public:
	// Copied from factory before PrepareForData.
	bool bMultiplyByEntryWeight = true;
	FPCGExInputShorthandSelectorDouble TimeSource;

	TSharedPtr<PCGExDetails::TSettingValue<double>> TimeGetter;

	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

protected:
	// Constant-t fast path, resolved in OnInitForData: the whole effective-weight table is
	// point-invariant, so precompute the cumulative once and reduce Pick to a single roll.
	bool bConstantTime = false;
	double ConstantTotalWeight = 0.0;
	TArray<double> ConstantCumulative; // parallel to ValidEntryIndices

	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;

	/** weight_i(t) for the ValidEntryIndices[LocalIdx] entry. */
	FORCEINLINE double EffectiveWeight(const int32 EntryIndex, const double Time) const
	{
		const double Response = FMath::Max(0.0, Shared->EntryLookups[EntryIndex]->Eval(Time));
		return bMultiplyByEntryWeight ? Response * Shared->EntryWeights[EntryIndex] : Response;
	}
};

/**
 * Factory data for Curve-Remapped Weight selection.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorCurveRemappedFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorCurveRemappedConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector : Curve-Remapped Weight".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/selector-curve-remapped-weight"))
class PCGEXCOLLECTIONS_API UPCGExSelectorCurveRemappedFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorCurveRemapped, "Selector : Curve-Remapped Weight",
		"Per-point t drives each entry's weight through that entry's authored Float Curve property. Curves double as range gates: entries whose curve is <= 0 at t drop out of the pool.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorCurveRemappedConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
