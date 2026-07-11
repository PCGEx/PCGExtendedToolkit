// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorAntiRepeat.generated.h"

/**
 * Selector-specific configuration for Anti-Repeat. Shared verbatim between the palette node
 * settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for serialization).
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorAntiRepeatConfig
{
	GENERATED_BODY()

	/** Number of most-recent picks that are excluded from re-picking. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1, UIMin=1))
	int32 HistoryLength = 1;

	/** Max re-rolls (with salted seeds) before accepting a repeat anyway. Guards categories smaller than the history. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1, UIMin=1))
	int32 MaxAttempts = 8;
};

/**
 * Per-scope scratch for Anti-Repeat: the pick-history ring plus the inner selector's own
 * scratch. The ring is the selector's actual state -- without a scope scratch, history is
 * empty on every pick and Anti-Repeat degrades to the inner pick (see op comment).
 */
class FPCGExAntiRepeatScratch : public FPCGExPickerScratchBase
{
public:
	TArray<int32, TInlineAllocator<8>> Ring; // most recent raw picks, cyclic
	int32 Head = 0;
	// Same-point re-invocation guard (e.g. Quota CAS-loss retries): RecordPick replaces the last
	// written slot when the point repeats, so one point never consumes more than one ring slot.
	int32 LastPointIndex = INDEX_NONE;
	int32 LastSlot = INDEX_NONE;
	TSharedPtr<FPCGExPickerScratchBase> ChildScratch;
};

/**
 * Repetition-breaking wrapper. Delegates the actual pick to an inner selector (or plain
 * weighted-random when none is connected), then re-rolls with salted seeds while the pick
 * matches one of the last K picks in this processing scope.
 *
 * SCOPE-LOCAL HISTORY: scopes are contiguous index ranges processed on one thread, so
 * along paths/splines this matches "no repeats along the run". History resets at scope
 * boundaries; points in different scopes never see each other's history.
 */
class PCGEXCOLLECTIONS_API FPCGExEntryAntiRepeatPickerOp : public FPCGExEntryPickerOperation
{
public:
	// Copied from factory before PrepareForData.
	int32 HistoryLength = 1;
	int32 MaxAttempts = 8;
	const UPCGExSelectorFactoryData* ChildFactory = nullptr; // null -> weighted-random inner pick

	TSharedPtr<FPCGExEntryPickerOperation> ChildOp;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection) override;
	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

private:
	/**
	 * Record the final pick for PointIndex. Same-point re-invocations (wrapper retries, e.g.
	 * Quota CAS-loss) replace the previous record instead of appending, keeping the look-back
	 * measured in points.
	 */
	void RecordPick(FPCGExAntiRepeatScratch& S, int32 PointIndex, int32 Raw) const;
};

/**
 * Factory data for Anti-Repeat selection. Composite shared data mirrors Cascade's pattern
 * but for the single optional child.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorAntiRepeatFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorAntiRepeatConfig Config;

	/** Optional inner selector. Null falls back to plain weighted-random picks. */
	UPROPERTY()
	TObjectPtr<const UPCGExSelectorFactoryData> Child;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector Modifier : Anti-Repeat".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/mod-anti-repeat"))
class PCGEXCOLLECTIONS_API UPCGExSelectorAntiRepeatFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorAntiRepeat, "Selector Modifier : Anti-Repeat",
		"Re-rolls the inner selector (weighted random when none is connected) while the pick matches one of the last K picks in the processing scope. Breaks visible repetition along paths and grids.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorAntiRepeatConfig Config;

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
