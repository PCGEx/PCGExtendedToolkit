// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGBasePointData.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorNoiseField.generated.h"

namespace PCGExNoise3D
{
	class FNoiseGenerator;
}

/**
 * Selector-specific configuration for Noise Field. Shared verbatim between the palette node
 * settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for serialization).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorNoiseFieldConfig
{
	GENERATED_BODY()

	/** When enabled, entries occupy noise-value bands proportional to their Weight; otherwise all entries get equal bands. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUseWeights = true;
};

/**
 * Collection-derived state for Noise Field: cumulative entry weights (same shape as
 * Interleaved's), built once per (Factory, Category).
 */
class FPCGExNoiseFieldSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<double> Cumulative; // parallel to Target->Entries
	double TotalWeight = 0.0;
};

/**
 * Spatially-coherent pick: samples the connected Noise3D stack at the point's world position
 * and maps the [0, 1] value through cumulative weight bands. Nearby points read similar noise
 * values and therefore pick the same entries -- organic patches without any attribute pre-pass.
 *
 * DETERMINISTIC BY POSITION: the per-point Seed is intentionally ignored (spatial coherence is
 * the point). Noise seeds/transforms on the connected Noise3D definitions drive variation.
 */
class PCGEXCOLLECTIONS_API FPCGExEntryNoiseFieldPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExNoiseFieldSharedData>
{
public:
	// Bound by the factory before PrepareForData. Thread-safe after init (see FNoiseGenerator).
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> Noise;
	bool bUseWeights = true;

	// Per-point position source -- cached at init, read directly in the hot path.
	TConstPCGValueRange<FTransform> TransformRange;

	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

protected:
	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;
};

/**
 * Factory data for Noise Field selection. The noise generator is built from the provider
 * node's "Noise" pin at factory-creation time and shared (read-only) by all ops.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorNoiseFieldFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorNoiseFieldConfig Config;

	/** Built from the provider's Noise pin in CreateFactory. Thread-safe after initialization. */
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> Noise;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector : Noise Field".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/selector-noise-field"))
class PCGEXCOLLECTIONS_API UPCGExSelectorNoiseFieldFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorNoiseField, "Selector : Noise Field",
		"Samples the connected Noise3D stack at each point's world position and maps the value through weight bands. Spatially coherent patches -- nearby points pick alike. Ignores the per-point seed.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorNoiseFieldConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
