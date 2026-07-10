// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"
#include "Selectors/PCGExSelectorHelpers.h"
#include "Selectors/PCGExSelectorSharedData.h"
#include "Types/PCGExTypeOpsNumeric.h"
#include "Types/PCGExTypeOpsRotation.h"
#include "Types/PCGExTypeOpsString.h"
#include "Types/PCGExTypeOpsVector.h"

#include "PCGExSelectorClosestMatch.generated.h"

#pragma region Distance traits

/**
 * Selector-local conveniences: trait predicates and the single-value Distance dispatcher.
 * All per-type primitives (Distance / MatchScore / RangeMagnitude / ExtendRange /
 * ComputeInvRange / ApplyRemap) live on PCGExTypeOps::FTypeOps<T>, gated by
 * PCGExTypes::TTraits<T>::bSupportsDistance / bSupportsMatchScore / bSupportsNormalization.
 */
namespace PCGExClosestMatch
{
	template <typename T>
	constexpr FORCEINLINE bool IsSupported()
	{
		return PCGExTypes::TTraits<T>::bSupportsDistance || PCGExTypes::TTraits<T>::bSupportsMatchScore;
	}

	template <typename T>
	constexpr FORCEINLINE bool IsNormalizable()
	{
		return PCGExTypes::TTraits<T>::bSupportsNormalization;
	}

	// Single-value distance dispatcher: routes to FTypeOps<T>::Distance for ordered types or
	// FTypeOps<T>::MatchScore for categorical types. Returns 0 for types not supporting either.
	template <typename T>
	FORCEINLINE double Distance(const T& A, const T& B)
	{
		if constexpr (PCGExTypes::TTraits<T>::bSupportsDistance)
		{
			return PCGExTypeOps::FTypeOps<T>::Distance(A, B);
		}
		else if constexpr (PCGExTypes::TTraits<T>::bSupportsMatchScore)
		{
			return PCGExTypeOps::FTypeOps<T>::MatchScore(A, B);
		}
		else
		{
			return 0.0;
		}
	}
}

#pragma endregion

#pragma region Axis + Config

/**
 * One axis: pairs a collection-side property name with a point-side attribute name.
 * The property's declared EPCGMetadataType determines the resolution type on both sides
 * (point attribute is broadcast to that type via FFacade::GetBroadcaster<T>).
 *
 * Weight scales the axis contribution to the total per-entry distance. bNormalize remaps
 * each side to [0, 1] independently using its own observed range -- see the field comment.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorClosestMatchAxis
{
	GENERATED_BODY()

	/** Collection property to read on the entry side. Drives the comparison type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName PropertyName = NAME_None;

	/** Point attribute to read on the query side. Broadcast to the property type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName AttributeName = NAME_None;

	/** Multiplier applied to this axis's per-entry distance before summing into the total. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	double Weight = 1.0;

	/**
	 * When true, each side is remapped to [0, 1] using its own observed range before distance
	 * is computed. Point side scans the broadcaster's min/max once at init; entry side uses the
	 * captured per-category min/max. Components with degenerate range contribute zero.
	 *
	 * Only effective for normalizable types (float/double/FVector/FRotator). Integer / bool /
	 * categorical / quaternion axes silently use raw distance regardless of this flag.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bNormalize = false;
};

USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExSelectorClosestMatchConfig
{
	GENERATED_BODY()

	FPCGExSelectorClosestMatchConfig()
		: Axes({FPCGExSelectorClosestMatchAxis()})
	{
	}

	/** One or more axes summed (with weights) into the per-entry distance score. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, TitleProperty="{PropertyName}"))
	TArray<FPCGExSelectorClosestMatchAxis> Axes;
};

#pragma endregion

#pragma region Shared data

/**
 * Type-erased base for per-axis shared collection-derived state. Subclassed by
 * TPCGExClosestMatchAxisData<T> with the concrete property type recorded.
 */
class PCGEXCOLLECTIONS_API FPCGExClosestMatchAxisData : public TSharedFromThis<FPCGExClosestMatchAxisData>
{
public:
	EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
	bool bHasMinMax = false; // true when EntryMin/EntryMax were populated (axis opted into normalize AND type supports it)
	int32 ResolvedCount = 0;
	TBitArray<> EntryResolved; // per-entry, sized to Target->Entries.Num()

	virtual ~FPCGExClosestMatchAxisData() = default;
};

template <typename T>
class TPCGExClosestMatchAxisData : public FPCGExClosestMatchAxisData
{
public:
	TArray<T> EntryValues;
	T EntryMin{};
	T EntryMax{};
};

/**
 * Collection-derived state for Closest Match. Built once per (Factory, Category) via the
 * factory's BuildSharedData override; reused across facades via FSelectorSharedDataCache.
 *
 * AxisData is parallel to Config.Axes -- entries left as nullptr where an axis failed to
 * resolve (unsupported type, missing property, no resolvable entries). ValidEntryIndices
 * is the intersection of resolved entries across all non-null axes.
 */
class PCGEXCOLLECTIONS_API FPCGExClosestMatchSharedData : public PCGExCollections::FSelectorSharedData
{
public:
	int32 AxisCount = 0;
	int32 EntryCount = 0;
	TArray<TSharedPtr<FPCGExClosestMatchAxisData>> AxisData;
	TArray<int32> ValidEntryIndices;
};

#pragma endregion

#pragma region Op

/**
 * Per-facade axis evaluator. Batched: one virtual Accumulate() per axis per pick (instead of
 * one virtual call per axis per entry), with the query attribute read and remapped exactly
 * once per pick. The typed subclass runs a tight non-virtual inner loop over entries.
 */
class PCGEXCOLLECTIONS_API FPCGExClosestMatchAxisEvaluator
{
public:
	double Weight = 1.0;
	virtual ~FPCGExClosestMatchAxisEvaluator() = default;

	/**
	 * Add this axis's weighted distance contribution for every listed entry.
	 * InOutDistances is parallel to EntryIndices (compact over ValidEntryIndices).
	 */
	virtual void Accumulate(int32 PointIndex, TConstArrayView<int32> EntryIndices, TArrayView<double> InOutDistances) const = 0;
};

template <typename T>
class TPCGExClosestMatchAxisEvaluator : public FPCGExClosestMatchAxisEvaluator
{
public:
	TSharedPtr<PCGExData::TBuffer<T>> Broadcaster;
	TSharedPtr<TPCGExClosestMatchAxisData<T>> Shared; // keeps EntryValues alive

	// When bRemap is true, the query is mapped to [0, 1] via PointMin/PointInvRange before
	// distance -- only set up for types satisfying IsNormalizable<T>. The entry side is
	// pre-remapped once at bind time into RemappedEntryValues.
	bool bRemap = false;
	T PointMin{};
	T PointInvRange{};

	// Entry-side values consumed by Accumulate: Shared->EntryValues directly, or the
	// pre-remapped copy when bRemap. Bound once in BuildEvaluatorT.
	TArray<T> RemappedEntryValues;
	const T* EntryValuesPtr = nullptr;

	virtual void Accumulate(int32 PointIndex, TConstArrayView<int32> EntryIndices, TArrayView<double> InOutDistances) const override
	{
		T Query = Broadcaster->Read(PointIndex);
		if constexpr (PCGExClosestMatch::IsNormalizable<T>())
		{
			if (bRemap)
			{
				Query = PCGExTypeOps::FTypeOps<T>::ApplyRemap(Query, PointMin, PointInvRange);
			}
		}

		const double W = Weight;
		const int32 Num = EntryIndices.Num();
		for (int32 j = 0; j < Num; ++j)
		{
			InOutDistances[j] += PCGExClosestMatch::Distance<T>(Query, EntryValuesPtr[EntryIndices[j]]) * W;
		}
	}
};

/**
 * Per-scope scratch for Closest Match picks: reusable per-entry distance accumulator.
 * Ops fall back to a per-pick local buffer when no scratch is provided.
 */
class FPCGExClosestMatchScratch : public FPCGExPickerScratchBase
{
public:
	TArray<double, TInlineAllocator<64>> Distances;
};

/**
 * Closest-match entry picker. Returns the entry whose weighted multi-axis distance to
 * the point's query values is smallest. Deterministic: first encountered minimum wins
 * (no random tie-break -- floating-point ties are vanishingly rare in practice).
 */
class PCGEXCOLLECTIONS_API FPCGExEntryClosestMatchPickerOp : public PCGExCollections::Selectors::TTypedSharedPickerOpBase<FPCGExClosestMatchSharedData>
{
public:
	// Copied from factory before PrepareForData.
	TArray<FPCGExSelectorClosestMatchAxis> Axes;

	// Built in OnInitForData via PCGEX_EXECUTEWITHRIGHTTYPE dispatch. Axes that fail to bind
	// (unsupported type, missing attribute) are skipped -- only successfully bound evaluators
	// land here, so the hot path never has to null-check.
	TArray<TSharedPtr<FPCGExClosestMatchAxisEvaluator>> ActiveEvaluators;

	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const override;

	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;

protected:
	virtual void OnSharedDataMissing(FPCGExContext* InContext) const override;
	virtual bool OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;
};

#pragma endregion

#pragma region Factory + provider

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorClosestMatchFactoryData : public UPCGExSelectorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSelectorClosestMatchConfig Config;

	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const override;
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Collections|Distribution", meta=(PCGExNodeLibraryDoc="staging/staging-distribute/selector-closest-match"))
class PCGEXCOLLECTIONS_API UPCGExSelectorClosestMatchFactoryProviderSettings : public UPCGExSelectorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		SelectorClosestMatch, "Selector : Closest Match",
		"Pick the entry whose weighted multi-axis distance to per-point query values is smallest.",
		FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Selector-specific configuration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorClosestMatchConfig Config;

	/** Shared distribution configuration (seed, entry distribution, categories). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};

#pragma endregion
