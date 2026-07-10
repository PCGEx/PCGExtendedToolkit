// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorCascade.generated.h"

/**
 * Composite shared data for Cascade: one slot per child factory, parallel to
 * UPCGExSelectorCascadeFactoryData::Children. Slots are null for children whose factories
 * don't produce shared data (or declined to). Built once per (CascadeFactory, Category) via
 * the cache -- children's shared data is deduped per category through the cascade's own
 * cache entry (cross-factory dedup with standalone uses of the same child is not attempted).
 */
class FPCGExCascadeSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<TSharedPtr<PCGExCollections::FSelectorSharedData>> PerChild;

	// Children built through a parent's BuildSharedData never see the cache -- forward so
	// batch-dependent child state (e.g. a nested Quota budget) still finalizes under the lock.
	virtual void OnCached(const PCGExCollections::FSelectorSharedDataCache& InCache) override
	{
		for (const TSharedPtr<PCGExCollections::FSelectorSharedData>& Child : PerChild)
		{
			if (Child)
			{
				Child->OnCached(InCache);
			}
		}
	}
};

/**
 * Composite per-scope scratch for Cascade: one slot per PREPARED child op (parallel to
 * FPCGExEntryCascadePickerOp::ChildOps, not to the factory's Children).
 */
class FPCGExCascadeScratch : public FPCGExPickerScratchBase
{
public:
	TArray<TSharedPtr<FPCGExPickerScratchBase>> PerChild;
};

/**
 * Fallback-chain entry picker. Children are tried in Priority order; the first valid pick
 * (!= -1) wins. Children whose PrepareForData fails are skipped at init (logged), so the
 * hot path only walks live ops. Child seeds are salted by child index so sibling random
 * rolls don't correlate; child 0 receives the unsalted seed, keeping a single-child cascade
 * pick-identical to that child running standalone.
 */
class PCGEXCOLLECTIONS_API FPCGExEntryCascadePickerOp : public FPCGExEntryPickerOperation
{
public:
	// Copied from factory before PrepareForData (raw pointers -- the factory's UPROPERTY array owns them).
	TArray<const UPCGExSelectorFactoryData*> ChildFactories;

	// Prepared child ops only. Parallel to ChildSalts.
	TArray<TSharedPtr<FPCGExEntryPickerOperation>> ChildOps;
	TArray<int32> ChildSalts;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection) override;
	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/**
 * Factory data for Cascade selection. Children arrive pre-sorted by Priority from
 * GetInputFactories. The cascade's own BaseConfig drives seed/category/micro concerns;
 * children's BaseConfigs are intentionally ignored (documented on the provider).
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorCascadeFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	/** Child selector factories, sorted by Priority (ascending) at collection time. */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGExSelectorFactoryData>> Children;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector Modifier : Cascade". Consumes an ordered set of selectors on the
 * "Selectors" pin; the first child returning a valid pick wins.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/staging-distribute/selector-cascade"))
class PCGEXCOLLECTIONS_API UPCGExSelectorCascadeFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorCascade, "Selector Modifier : Cascade",
		"Fallback chain: tries each connected selector in Priority order, first valid pick wins. Children's seed/category/micro settings are ignored -- this node's Base Config drives those.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Shared distribution configuration (seed, entry distribution, categories). Children's BaseConfigs are ignored. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
