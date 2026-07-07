// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Helpers/PCGExCollectionsHelpers.h"

#include "PCGExStagingSwap.generated.h"

/**
 * Swap staged entry picks to variant collection entries. Rewrites per-point pick hashes
 * (Tag_EntryIdx) from (SourceCollectionGUID, RawIndex) to the variant's (GUID, RawIndex)
 * using the variant's baked mapping, and emits an updated Collection Map that includes the
 * variant collections. Points whose entry has no override pass through untouched.
 *
 * Place after a staging node (picks must exist) and before consumers (spawn/load/selector
 * nodes). Fitting transforms were computed against the SOURCE entry's bounds -- re-adapt with
 * the dedicated Staging : Fitting node downstream if the variant assets differ in footprint.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage swap variant biome theme skin collection", PCGExNodeLibraryDoc="staging/staging-swap"))
class UPCGExStagingSwapSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(StagingSwap, "Staging : Swap", "Swap staged entry picks to variant collection entries (biomes, themes) by rewriting pick hashes.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}
	
	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points are considered for swapping.", PCGExFactories::PointFilters(), false)
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/**
	 * Variant collections to apply -- each slot is a constant path or a `@Data` attribute read
	 * per input data, so different inputs can resolve different variants. Each variant's source
	 * groups are matched against the collections present in the input Collection Map; groups
	 * whose source isn't in the map are ignored. Later slots win on conflicting mappings.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, AllowedClasses="/Script/PCGExCollections.PCGExVariantCollection"))
	TArray<FPCGExInputShorthandNameSoftObjectPath> VariantCollections;

	/**
	 * Skip source groups whose baked mapping is stale against the live source collection
	 * (entries were added/removed/reordered since the variant was last saved) instead of
	 * applying a potentially wrong mapping. Re-save the variant asset to refresh its bake.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSkipStaleMappings = true;
	
	/** Suppress no applicable variant warnings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietNoApplicableVariantsWarning = false;
};

struct FPCGExStagingSwapContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingSwapElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;
	TSharedPtr<PCGExCollections::FPickPacker> CollectionPickDatasetPacker;

	/**
	 * Per-input-IO variant paths (keyed by IOIndex) resolved during Boot, in slot order
	 * (later slots win on conflicting mappings). Batch-loaded via RegisterAssetDependencies;
	 * consumed by PostLoadAssetsDependencies to build SwapMapsPerIO.
	 */
	TMap<int32, TArray<FSoftObjectPath>> VariantPathsPerIO;

	/**
	 * Per-input-IO swap map (keyed by IOIndex): H64(SourceCollectionGUID, SourceRawIndex) ->
	 * full replacement pick hash (secondary pick reset). Per-IO because variants can be
	 * @Data-driven; single-variant IOs share one contribution map, multi-variant IOs merge.
	 */
	TMap<int32, TSharedPtr<TMap<uint64, uint64>>> SwapMapsPerIO;

	/** Registers every resolved variant path for batch pre-loading. */
	virtual void RegisterAssetDependencies() override;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingSwapElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingSwap)

	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExStagingSwap
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingSwapContext, UPCGExStagingSwapSettings>
	{
	protected:
		TSharedPtr<PCGExData::TBuffer<int64>> HashReader;
		TSharedPtr<PCGExData::TBuffer<int64>> HashWriter;
		TSharedPtr<TMap<uint64, uint64>> SwapMap;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
