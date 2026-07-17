// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorQuota.generated.h"

/** How the per-entry quota property values translate into pick counts. */
UENUM()
enum class EPCGExQuotaMode : uint8
{
	Count      = 0 UMETA(DisplayName = "Count", ToolTip="Property values are absolute pick counts."),
	Proportion = 1 UMETA(DisplayName = "Proportion", ToolTip="Property values in [0, 1] are shares of the processed point count."),
};

/** Which point population the caps apply to. Values are stable; declaration order drives the dropdown (default first). */
UENUM()
enum class EPCGExQuotaScope : uint8
{
	PerInputData = 1 UMETA(DisplayName = "Per Input Data", ToolTip="Each input data gets its own independent caps."),
	AllInputs    = 0 UMETA(DisplayName = "All Inputs", ToolTip="Caps apply to the whole batch: each global cap is pre-split across inputs proportionally to their point counts (deterministic; unused budget does not spill over between inputs). Requires the consumer to wire a selector shared-data cache with per-input totals (Staging Distribute and Spline Mesh do); falls back to a first-come shared pool otherwise."),
};

/** What to do when every available entry is exhausted. */
UENUM()
enum class EPCGExQuotaExhaustedBehavior : uint8
{
	Skip        = 0 UMETA(DisplayName = "Skip Point", ToolTip="Return an invalid pick so the consuming node skips the point."),
	IgnoreQuota = 1 UMETA(DisplayName = "Ignore Quota", ToolTip="Fall back to an unconstrained pick from the inner selector."),
};

/**
 * Selector-specific configuration for Quota. Shared verbatim between the palette node
 * settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for serialization).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorQuotaConfig
{
	GENERATED_BODY()

	/**
	 * Numeric collection property supplying each entry's MAX pick count (cap).
	 * Entries without the property (or with negative values) are uncapped; zero = never pickable.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName MaxPropertyName = NAME_None;

	/**
	 * Optional numeric collection property supplying each entry's MIN pick count. Minimums are
	 * satisfied by deterministic low-discrepancy reservation (accurate to ±1, spread evenly
	 * across point order) and always resolve PER INPUT DATA. Entries without the property
	 * have no minimum.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName MinPropertyName = NAME_None;

	/** How property values translate into counts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExQuotaMode Mode = EPCGExQuotaMode::Count;

	/** Which point population the MAX caps apply to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExQuotaScope Scope = EPCGExQuotaScope::PerInputData;

	/** Behavior once every available entry is exhausted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExQuotaExhaustedBehavior ExhaustedBehavior = EPCGExQuotaExhaustedBehavior::Skip;
};

/**
 * Collection-derived state for Quota: per-entry raw quota values + weights, plus -- for the
 * AllInputs scope -- the shared atomic counter block itself. The counters are the sanctioned
 * exception to the shared-data read-only contract: lock-free atomics, mutated only through
 * FPCGExPickAvailability semantics, safe for concurrent facades by construction.
 */
class FPCGExQuotaSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	TArray<double> EntryWeights; // (Weight + 1), parallel to Target->Entries
	TArray<double> MaxValues;    // raw property values; < 0 (or unresolved) = uncapped
	TArray<double> MinValues;    // raw property values; <= 0 (or unresolved) = no minimum

	// AllInputs scope only. Count mode: initialized from MaxValues at build. Proportion mode:
	// finalized against the batch total in OnCached; starts empty-capped (0) with a per-facade
	// grow fallback when no total was provided (see bBudgetFinalized).
	// LEGACY path: only consumed when PerInputPointCounts is unavailable -- with per-input
	// counts, ops slice the global caps deterministically and use local counters instead.
	TUniquePtr<std::atomic<int32>[]> SharedRemaining;

	// Per-input In-side point counts (by IOIndex), copied from the cache in OnCached. Non-empty
	// enables the deterministic AllInputs pre-split; empty falls back to SharedRemaining.
	TArray<int64> PerInputPointCounts;

	// bProportionBudget: counters are proportion-mode (set at build). bBudgetFinalized: OnCached
	// filled them from the batch total -- ops skip the per-facade fallback. Plain bools: written
	// under the cache lock, before publication.
	bool bProportionBudget = false;
	bool bBudgetFinalized = false;

	// Inner selector's shared data (single-slot FPCGExCascadeSharedData, mirrors Anti-Repeat).
	TSharedPtr<PCGExCollections::FSelectorSharedData> ChildSharedData;

	virtual void OnCached(const PCGExCollections::FSelectorSharedDataCache& InCache) override;
};

/**
 * Per-scope scratch for Quota picks: hosts the inner selector's scratch.
 */
class FPCGExQuotaScratch : public FPCGExPickerScratchBase
{
public:
	TSharedPtr<FPCGExPickerScratchBase> ChildScratch;
};

/**
 * Quota decorator: delegates picking to an inner selector (plain weighted-random when none is
 * connected) through the PickFiltered contract, so exhausted entries are excluded inside the
 * inner selector's own candidate loop. Capacity is claimed via CAS; a lost race re-invokes the
 * inner pick with the entry now unavailable, so even fully deterministic inners converge.
 *
 * MIN quotas are satisfied by deterministic low-discrepancy reservation: a stable fraction of
 * points (spread evenly by the golden-ratio sequence over point order) is force-assigned to
 * under-minimum entries before the inner selector runs. Accurate to ±1 per entry, per input data.
 *
 * MAX counts are EXACT. WHICH points receive capped entries is deterministic (point-index-order
 * claim priority) when the consumer drives the pre-resolve passes (Staging Distribute and Spline
 * Mesh do): a parallel first-choice pass, then a sequential point-order commit that claims
 * capacity. Consumers that skip pre-resolve fall back to the legacy live CAS path, where the
 * point assignment depends on thread scheduling.
 */
class PCGEXCOLLECTIONS_API FPCGExEntryQuotaPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExQuotaSharedData>
{
public:
	// Copied from factory before PrepareForData.
	EPCGExQuotaMode Mode = EPCGExQuotaMode::Count;
	EPCGExQuotaScope Scope = EPCGExQuotaScope::PerInputData;
	EPCGExQuotaExhaustedBehavior ExhaustedBehavior = EPCGExQuotaExhaustedBehavior::Skip;
	double ReservationPhase = 0.0;
	const UPCGExSelectorFactoryData* ChildFactory = nullptr; // null -> weighted-random inner pick

	TSharedPtr<FPCGExEntryPickerOperation> ChildOp;

	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;

	virtual bool WantsPreResolve() const override;
	virtual void BeginPreResolve(int32 NumPoints) override;
	virtual void PreResolveFirstChoice(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) override;
	virtual void CommitPreResolve(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) override;

protected:
	// Effective counter block: points at Shared->SharedRemaining (AllInputs) or LocalRemaining
	// (PerInputData). Resolved once at init; Pick builds its availability view over it.
	std::atomic<int32>* Remaining = nullptr;
	TUniquePtr<std::atomic<int32>[]> LocalRemaining;
	int32 NumEntries = 0;

	// Raw collection index -> category-local index, built once at init (inner selectors return
	// raw indices; counters and availability are category-local).
	TArray<int32> RawToLocal;

	// Min-reservation state, per facade. Reserved points map through cumulative min counts.
	TArray<int32> MinLocalEntries;         // category-local entries with a minimum
	TArray<double> MinCumulativeCounts;    // parallel to MinLocalEntries
	double TotalMinCount = 0.0;
	double ReservationFraction = 0.0;      // TotalMinCount / NumPoints, clamped to [0, 1]
	double NumPointsD = 0.0;

	// Pre-resolve state (see FPCGExEntryPickerOperation contract). NotResolved marks
	// "not computed" in both arrays -- committed points short-circuit Pick(), anything
	// else falls back to the live path.
	static constexpr int32 NotResolved = -2;
	bool bHasMaxCaps = false;
	TArray<int32> FirstChoice; // raw pick vs initial availability; -1 = inner found none
	TArray<int32> Resolved;    // final raw pick per point

	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;

	/** Resolve a raw quota property value into a count for this facade. */
	int32 ResolveCount(double RawValue) const;

	/** Golden-ratio low-discrepancy value deciding min-reservation membership for a point. */
	double ReservationU(int32 PointIndex) const;

	/**
	 * Shared pick body: min reservation, then filtered inner pick + capacity claim.
	 * SeededRaw != NotResolved short-circuits attempt 0 with a pre-computed first choice
	 * (pre-resolve commit pass); every retry re-picks against live availability.
	 */
	int32 ResolvePick(int32 PointIndex, int32 Seed, int32 SeededRaw, FPCGExPickerScratchBase* Scratch) const;
};

/**
 * Factory data for Quota selection.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorQuotaFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorQuotaConfig Config;

	/** Optional inner selector. Null falls back to plain weighted-random picks. */
	UPROPERTY()
	TObjectPtr<const UPCGExSelectorFactoryData> Child;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector Modifier : Quota".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/mod-quota"))
class PCGEXCOLLECTIONS_API UPCGExSelectorQuotaFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorQuota, "Selector Modifier : Quota",
		"Wraps any selector with exact per-entry max caps and near-exact (±1) min guarantees sourced from numeric collection properties. Deterministic: capped entries are claimed in point-index order.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorQuotaConfig Config;

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
