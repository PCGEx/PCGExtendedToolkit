// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"

#include "PCGExSelectorRangeBased.generated.h"

/** How per-entry Min/Max are sourced from CollectionProperties. */
UENUM()
enum class EPCGExRangeSourceMode : uint8
{
	TwoNumerics = 0 UMETA(DisplayName = "XX", ToolTip="Pick two numeric properties (Double/Float/Int) for Min and Max."),
	Vector2     = 1 UMETA(DisplayName = "XY", ToolTip="Pick a single Vector2D/Vector friendly property; X is Min, Y is Max."),
};

/** What to do when a point's value falls inside multiple entries' ranges. */
UENUM()
enum class EPCGExRangeOverlapMode : uint8
{
	WeightedRandom = 0 UMETA(DisplayName = "Weighted Random", ToolTip="Pick weighted-random by Entry.Weight among all entries whose range contains the value."),
	FirstMatch     = 1 UMETA(DisplayName = "First Match", ToolTip="Pick the first entry (in category order) whose range contains the value. Weight is ignored."),
	NarrowestWins  = 2 UMETA(DisplayName = "Narrowest Wins", ToolTip="Pick the entry with the smallest (Max - Min) among matches. Weight breaks ties on equal widths."),
};

/** Inclusivity at range boundaries. */
UENUM()
enum class EPCGExRangeBoundaryMode : uint8
{
	ClosedClosed = 0 UMETA(DisplayName = "[|]", ToolTip="Both endpoints match.", ActionIcon="ClosedClosed"),
	ClosedOpen   = 1 UMETA(DisplayName = "[|<", ToolTip="Min matches, Max does not.", ActionIcon="ClosedOpen"),
	OpenClosed   = 2 UMETA(DisplayName = ">|]", ToolTip="Min does not match, Max does.", ActionIcon="OpenClosed"),
	OpenOpen     = 3 UMETA(DisplayName = ">|<", ToolTip="Neither endpoint matches.", ActionIcon="OpenOpen"),
};

/**
 * One range axis: per-point value driver + per-entry range property source(s) + boundary mode.
 *
 * Designed to be used both standalone (single-axis Range-Based selection) and inside a
 * TArray for multi-axis (N-D AND-policy) selection. Each axis is independent -- its own
 * value source, its own property sources, its own boundary mode.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorRangeAxis
{
	GENERATED_BODY()

	/** Per-point value matched against each entry's authored range on this axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExInputShorthandSelectorDouble ValueSource = FPCGExInputShorthandSelectorDouble(FName("$Density"), 1.0, true);

	/** How per-entry Min/Max are sourced from CollectionProperties. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExRangeSourceMode SourceMode = EPCGExRangeSourceMode::TwoNumerics;

	/** Name of the numeric property that defines each entry's Min. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SourceMode == EPCGExRangeSourceMode::TwoNumerics", EditConditionHides))
	FName MinPropertyName = NAME_None;

	/** Name of the numeric property that defines each entry's Max. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SourceMode == EPCGExRangeSourceMode::TwoNumerics", EditConditionHides))
	FName MaxPropertyName = NAME_None;

	/** Name of the Vector2D/Vector property whose X=Min, Y=Max. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SourceMode == EPCGExRangeSourceMode::Vector2", EditConditionHides))
	FName RangePropertyName = NAME_None;

	/** Inclusivity at range boundaries. Per-axis -- each axis can independently include/exclude its endpoints. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExRangeBoundaryMode BoundaryMode = EPCGExRangeBoundaryMode::OpenOpen;
};

/**
 * Selector-specific configuration for Range-Based selection. Shared verbatim between the
 * palette node settings (EditAnywhere, drives the UI) and the FactoryData (UPROPERTY for
 * serialization). Eliminates the field-by-field copy in CreateFactory.
 *
 * One or more axes form an AND-gated multidimensional range test: a candidate entry passes
 * only when the per-axis point value falls inside that entry's authored range on every axis.
 * With Axes.Num() == 1 this reduces to the classic single-axis range selector.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorRangeBasedConfig
{
	GENERATED_BODY()

	FPCGExSelectorRangeBasedConfig()
		: Axes({FPCGExSelectorRangeAxis()})
	{
	}

	/** One or more axes. With multiple axes, an entry must contain the point value on every axis (AND policy). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, TitleProperty="{MinPropertyName}"))
	TArray<FPCGExSelectorRangeAxis> Axes;

	/** What to do when the point value falls in multiple entries' ranges. For multi-axis, NarrowestWins scores by hypervolume (product of per-axis widths). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExRangeOverlapMode OverlapMode = EPCGExRangeOverlapMode::WeightedRandom;
};

/**
 * Collection-derived state for Range-Based selection. Multi-axis layout: per-entry × per-axis
 * mins and maxs in a flat entry-major array (Mins[E * AxisCount + A]) -- sequential per-entry
 * read pattern is cache-friendly for the AND-gated early-exit scan.
 */
class FPCGExRangeBasedSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	int32 AxisCount = 0;
	int32 EntryCount = 0;

	// Entry-major flat: EntryMins[E * AxisCount + A], EntryMaxs[E * AxisCount + A].
	TArray<double> EntryMins;
	TArray<double> EntryMaxs;

	// Boundary mode per axis (consumed by ContainsOnAxis in the hot path).
	TArray<EPCGExRangeBoundaryMode> AxisBoundaryModes;

	// Entries where every axis resolved a usable range. Invalid entries are excluded entirely.
	TArray<int32> ValidEntryIndices;

	// (Weight + 1) per entry; 0 for entries excluded from ValidEntryIndices.
	TArray<double> EntryWeights;
};

/**
 * Per-scope scratch for Range-Based picks: reusable per-axis value + match buffers so per-pick
 * work stops heap-allocating once the scope is warmed up. Ops fall back to per-pick local
 * buffers when no scratch is provided.
 */
class FPCGExRangeBasedScratch : public FPCGExPickerScratchBase
{
public:
	TArray<double, TInlineAllocator<8>> PointValues;
	TArray<int32, TInlineAllocator<32>> Matches;
	TArray<double, TInlineAllocator<32>> Cumulative;
	TArray<int32, TInlineAllocator<8>> TieBucket;
};

/**
 * Shared base for Range-Based picker operations. Holds per-axis value getters and a typed
 * pointer to shared collection-derived state. Concrete subclasses implement Pick() according
 * to their overlap policy.
 */
class FPCGExEntryRangeBasedPickerOpBase : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExRangeBasedSharedData>
{
public:
	// Copied from factory before PrepareForData. Used to init per-axis getters; ValueSource
	// fields are read at init only -- the resolved getters are what the hot path uses.
	TArray<FPCGExSelectorRangeAxis> Axes;

	// One getter per axis, parallel to Axes. Resolved in OnInitForData.
	TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> ValueGetters;

	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;

	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override = 0;

protected:
	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;

	/** Apply per-axis BoundaryMode to a (Value, Min, Max) test. AxisIdx must be valid for Shared->AxisBoundaryModes. */
	FORCEINLINE bool ContainsOnAxis(int32 AxisIdx, double V, double Min, double Max) const
	{
		switch (Shared->AxisBoundaryModes[AxisIdx])
		{
		default:
		case EPCGExRangeBoundaryMode::ClosedOpen:
			return V >= Min && V < Max;
		case EPCGExRangeBoundaryMode::ClosedClosed:
			return V >= Min && V <= Max;
		case EPCGExRangeBoundaryMode::OpenClosed:
			return V > Min && V <= Max;
		case EPCGExRangeBoundaryMode::OpenOpen:
			return V > Min && V < Max;
		}
	}

	/**
	 * AND-test: returns true iff entry `EntryIdx` contains the per-axis point values on every axis.
	 * Early-exits on the first axis miss.
	 */
	FORCEINLINE bool MatchesAllAxes(int32 EntryIdx, const double* PointValues) const
	{
		const int32 AxisCount = Shared->AxisCount;
		const int32 Base = EntryIdx * AxisCount;
		const TArray<double>& Mins = Shared->EntryMins;
		const TArray<double>& Maxs = Shared->EntryMaxs;
		for (int32 A = 0; A < AxisCount; ++A)
		{
			if (!ContainsOnAxis(A, PointValues[A], Mins[Base + A], Maxs[Base + A]))
			{
				return false;
			}
		}
		return true;
	}

	/** Read each axis's per-point value into OutValues. Inline-8 covers typical configs. */
	FORCEINLINE void ReadPointValues(int32 PointIndex, TArray<double, TInlineAllocator<8>>& OutValues) const
	{
		const int32 AxisCount = Shared->AxisCount;
		OutValues.SetNumUninitialized(AxisCount);
		for (int32 A = 0; A < AxisCount; ++A)
		{
			OutValues[A] = ValueGetters[A]->Read(PointIndex);
		}
	}
};

/** Weighted-random pick among entries whose range contains the value. */
class FPCGExEntryRangeWeightedRandomPickerOp : public FPCGExEntryRangeBasedPickerOpBase
{
public:
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/** First (in category order) entry whose range contains the value. */
class FPCGExEntryRangeFirstMatchPickerOp : public FPCGExEntryRangeBasedPickerOpBase
{
public:
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/** Narrowest (smallest Max - Min) entry whose range contains the value; weighted-random on width ties. */
class FPCGExEntryRangeNarrowestPickerOp : public FPCGExEntryRangeBasedPickerOpBase
{
public:
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/**
 * Factory data for Range-Based selection. Per-entry ranges are sourced from
 * CollectionProperties (either two numeric properties for Min/Max, or a single
 * Vector2D property where X=Min, Y=Max). Per-point scalar value drives the pick.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorRangeBasedFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorRangeBasedConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

/**
 * Palette node: "Selector : Range-Based".
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/selectors/selector-range-based"))
class PCGEXCOLLECTIONS_API UPCGExSelectorRangeBasedFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorRangeBased, "Selector : Range-Based",
		"Pick entries whose authored range contains a per-point value. Supports two source modes and three overlap policies.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorRangeBasedConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
