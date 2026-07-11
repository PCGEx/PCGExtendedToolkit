// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorDensityWeighted.generated.h"

/** Algorithm flavor for Density-Weighted distribution. */
UENUM()
enum class EPCGExDensityWeightMode : uint8
{
	WeightModulation     = 0 UMETA(DisplayName = "Weight Modulation", ToolTip="Per-point effective weight = entry.Weight * f(density). Higher density prefers higher-weight entries."),
	RandomnessModulation = 1 UMETA(DisplayName = "Randomness Modulation", ToolTip="density=1 picks purely weighted-random; density=0 picks uniform-random; blends linearly between."),
};

/** What to do when the density value falls outside the expected [0, 1] range. */
UENUM()
enum class EPCGExDensityOutOfRangePolicy : uint8
{
	Clamp     = 0 UMETA(DisplayName = "Clamp", ToolTip="Clamp the density value to [0, 1] before picking."),
	SkipPoint = 1 UMETA(DisplayName = "Skip Point", ToolTip="Return an invalid pick result so the consuming node skips the point."),
};

/**
 * Selector-specific configuration for Density-Weighted. Shared verbatim between the palette
 * node settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for serialization).
 * Eliminates the field-by-field copy in CreateFactory.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorDensityWeightedConfig
{
	GENERATED_BODY()

	/** Algorithm for how density influences the pick. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDensityWeightMode Mode = EPCGExDensityWeightMode::RandomnessModulation;

	/** Per-point density source (attribute or constant). Defaults to the point's $Density property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExInputShorthandSelectorDouble DensitySource = FPCGExInputShorthandSelectorDouble(FName("$Density"), 1.0, true);

	/** Blend factor (0..1) between plain weighted random (0) and full density-driven pick (1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, ClampMax=1, UIMin=0, UIMax=1))
	double DensityInfluence = 1.0;

	/** Behavior when density falls outside the expected [0, 1] range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDensityOutOfRangePolicy OutOfRangePolicy = EPCGExDensityOutOfRangePolicy::Clamp;
};

/**
 * Collection-derived state for Density-Weighted. Caches per-entry effective weight (Weight + 1)
 * and its natural log so the WeightModulation branch can resolve `Pow(W, Exp)` as the cheaper
 * `exp(LogW * Exp)` -- one transcendental per entry per pick instead of two. The RandomnessModulation
 * branch reads EntryWeights to skip per-pick entry dereferences.
 */
class FPCGExDensityWeightedSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<double> EntryWeights;    // (Weight + 1), parallel to Target->Entries
	TArray<double> EntryLogWeights; // log(EntryWeights[i]) -- for the WeightModulation fast path
	double TotalWeight = 0.0;       // Sum of EntryWeights -- analytic totals skip per-pick accumulation
};

/**
 * Per-scope scratch for Density-Weighted picks: reusable effective-weight buffer for the
 * WeightModulation path (the only one that materializes per-entry weights per pick).
 * Ops fall back to a per-pick local buffer when no scratch is provided.
 */
class FPCGExDensityWeightedScratch : public FPCGExPickerScratchBase
{
public:
	TArray<double, TInlineAllocator<32>> EffectiveWeights;
};

/**
 * Density-driven weighted pick operation. Reads a per-point density value and modulates the
 * pick using one of two algorithms (see EPCGExDensityWeightMode).
 *
 * Not shared with the base factory's micro dispatch (density-weighting applies to entry picks
 * only, not to material variant picks). Lives alongside the factory UCLASSes.
 */
class FPCGExEntryDensityWeightedPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExDensityWeightedSharedData>
{
public:
	EPCGExDensityWeightMode Mode = EPCGExDensityWeightMode::RandomnessModulation;
	double DensityInfluence = 1.0;
	EPCGExDensityOutOfRangePolicy OutOfRangePolicy = EPCGExDensityOutOfRangePolicy::Clamp;
	FPCGExInputShorthandSelectorDouble DensitySource;

	TSharedPtr<PCGExDetails::TSettingValue<double>> DensityGetter;

	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;

	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

protected:
	// Constant-density fast path, resolved in OnInitForData: when the density source is a
	// constant, the whole effective-weight table is point-invariant -- precompute the cumulative
	// once and reduce Pick to a single roll. bSkipAllPoints covers a constant out-of-range
	// density under the SkipPoint policy.
	bool bConstantDensity = false;
	bool bSkipAllPoints = false;
	double ConstantTotalWeight = 0.0;
	TArray<double> ConstantCumulative;

	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;
};

/**
 * Factory data for Density-Weighted distribution. Per-point density modulates the pick
 * distribution. Two algorithms are available via Mode (see EPCGExDensityWeightMode).
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorDensityWeightedFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorDensityWeightedConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector : Density-Weighted".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/selector-density-weighted"))
class PCGEXCOLLECTIONS_API UPCGExSelectorDensityWeightedFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorDensityWeighted, "Selector : Density-Weighted",
		"Per-point density modulates the pick distribution. Supports weight-modulation and randomness-modulation algorithms.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorDensityWeightedConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
