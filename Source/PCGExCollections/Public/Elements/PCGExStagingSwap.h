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

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

/**
 * One variant layer for an input IO, in VariantCollections slot order (later wins). Either uniform (one
 * contribution map for all points, from a constant/@Data slot) or per-point (variant looked up by loader
 * key). ProcessPoints keeps the last layer that remaps the point's current pick.
 */
struct FPCGExStagingSwapVariantLayer
{
	/** Uniform slot: one contribution for the whole IO. Null on per-point layers. */
	TSharedPtr<TMap<uint64, uint64>> Uniform;

	/** Per-point slot: per-point variant keys + hash -> contribution. Null on uniform layers. */
	TSharedPtr<TArray<PCGExValueHash>> PerPointKeys;
	TSharedPtr<TMap<PCGExValueHash, TSharedPtr<TMap<uint64, uint64>>>> PerPointContribution;

	/** Contribution governing the given point, or null if this layer doesn't apply. */
	const TMap<uint64, uint64>* ResolveContribution(const int32 Index) const
	{
		if (Uniform) { return Uniform.Get(); }
		check(PerPointKeys && PerPointContribution); // per-point layers always set both at assembly
		const TSharedPtr<TMap<uint64, uint64>>* Found = PerPointContribution->Find((*PerPointKeys)[Index]);
		return Found ? Found->Get() : nullptr;
	}
};

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
	 * Variant collections to apply -- each slot is a constant path, a `@Data` attribute read once
	 * per input data, or a per-point attribute resolving a different variant for every point. The
	 * attribute's domain is auto-detected; the node reads a per-point variant only when the attribute
	 * actually lives on the point (Elements) domain. Each variant's source groups are matched against
	 * the collections present in the input Collection Map; groups whose source isn't in the map are
	 * ignored. Later slots win on conflicting mappings (evaluated per point).
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
	 * consumed by PostLoadAssetsDependencies to build SwapMapsPerIO. Populated only on the
	 * all-uniform fast path (no per-point slots).
	 */
	TMap<int32, TArray<FSoftObjectPath>> VariantPathsPerIO;

	/**
	 * Per-input-IO swap map (keyed by IOIndex): H64(SourceCollectionGUID, SourceRawIndex) ->
	 * full replacement pick hash (secondary pick reset). Per-IO because variants can be
	 * @Data-driven; single-variant IOs share one contribution map, multi-variant IOs merge.
	 * Used only on the all-uniform fast path.
	 */
	TMap<int32, TSharedPtr<TMap<uint64, uint64>>> SwapMapsPerIO;

	/** True when at least one VariantCollections slot reads a per-point (non-@Data) attribute. */
	bool bHasPerPointSlots = false;

	/** Per-point variant loaders, indexed by VariantCollections slot; null for constant/@Data slots. */
	TArray<TSharedPtr<PCGEx::TAssetLoader<UPCGExVariantCollection>>> SlotLoaders;

	/** Per-point slot hash -> contribution, indexed by slot (null for constant/@Data slots); built in PostLoad. */
	TArray<TSharedPtr<TMap<PCGExValueHash, TSharedPtr<TMap<uint64, uint64>>>>> SlotContributionByHash;

	/**
	 * Per-IO uniform slot paths (keyed by IOIndex), sized to VariantCollections; per-point slots hold a
	 * null placeholder so PostLoad can assemble layers with original slot order preserved. Mixed path only.
	 */
	TMap<int32, TArray<FSoftObjectPath>> UniformSlotPathsPerIO;

	/** Per-IO ordered variant layers (keyed by IOIndex), built in PostLoad, consumed per point (later wins). */
	TMap<int32, TArray<FPCGExStagingSwapVariantLayer>> LayersPerIO;

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

		// All-uniform fast path: one merged map for the whole IO.
		TSharedPtr<TMap<uint64, uint64>> SwapMap;

		// Per-point path: this IO's ordered layers (points into context; later wins). Null on the fast path.
		const TArray<FPCGExStagingSwapVariantLayer>* Layers = nullptr;

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
