// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorRangeBased.h"

#include "PCGExPropertyTypes.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Selectors/PCGExSelectorHelpers.h"

namespace PCGExSelectorRangeBased
{
	// Relative tie tolerance for NarrowestWins hypervolume comparison. Hypervolumes span many
	// orders of magnitude (normalized 0..1 ranges vs world-scale ranges), so an absolute epsilon
	// either lumps genuinely different widths into ties or never fires -- scale the tolerance by
	// the smallest hypervolume seen. The absolute floor only covers exactly-zero-width ranges.
	constexpr double RangeTieRelEpsilon = 1e-9;
	constexpr double RangeTieAbsFloor = 1e-12;

	FORCEINLINE double RangeTieTolerance(const double MinHypervolume)
	{
		return FMath::Max(MinHypervolume * RangeTieRelEpsilon, RangeTieAbsFloor);
	}

	// Resolve a numeric property on an entry as double. Accepts any PCG-numeric property
	// (Double/Float/Int32/Int64/Bool) transparently via the type-erased TryGetPropertyValue.
	bool ResolveNumericAsDouble(const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Collection, FName Name, double& OutValue)
	{
		return Entry->TryGetPropertyValue<double>(Collection, Name, OutValue);
	}

	// Resolve a Vector2D-style range property on an entry. Any vector-compatible property
	// projects via TryGetPropertyValue<FVector2D> -- FVector drops Z to give (X, Y),
	// FVector4 drops Z/W. X -> Min, Y -> Max.
	bool ResolveRangeFromVector(const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Collection, FName Name, double& OutMin, double& OutMax)
	{
		FVector2D XY = FVector2D::ZeroVector;
		if (!Entry->TryGetPropertyValue<FVector2D>(Collection, Name, XY))
		{
			return false;
		}
		OutMin = XY.X;
		OutMax = XY.Y;
		return true;
	}

	// Resolve one axis's (Min, Max) for one entry, applying SourceMode + auto-swap.
	// Returns false if the property could not be resolved on this entry/axis combination.
	bool ResolveAxisForEntry(const FPCGExSelectorRangeAxis& Axis, const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Collection, double& OutMin, double& OutMax)
	{
		bool bResolved = false;
		double Min = 0.0;
		double Max = 0.0;
		switch (Axis.SourceMode)
		{
		case EPCGExRangeSourceMode::TwoNumerics:
			bResolved = ResolveNumericAsDouble(Entry, Collection, Axis.MinPropertyName, Min)
				&& ResolveNumericAsDouble(Entry, Collection, Axis.MaxPropertyName, Max);
			break;
		case EPCGExRangeSourceMode::Vector2:
			bResolved = ResolveRangeFromVector(Entry, Collection, Axis.RangePropertyName, Min, Max);
			break;
		}
		if (!bResolved)
		{
			return false;
		}
		// Auto-swap out-of-order ranges -- matches PCGEx convention for numeric range inputs.
		if (Min > Max)
		{
			Swap(Min, Max);
		}
		OutMin = Min;
		OutMax = Max;
		return true;
	}
}

#pragma region FPCGExEntryRangeBasedPickerOpBase

void FPCGExEntryRangeBasedPickerOpBase::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Range-Based -- no entries resolved every configured axis. Check property names and types in the collection."))
}

bool FPCGExEntryRangeBasedPickerOpBase::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	const int32 AxisCount = Axes.Num();
	if (AxisCount == 0)
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Range-Based -- Axes array is empty. At least one axis is required."))
		return false;
	}

	ValueGetters.SetNum(AxisCount);
	for (int32 A = 0; A < AxisCount; ++A)
	{
		ValueGetters[A] = Axes[A].ValueSource.GetValueSetting();
		if (!ValueGetters[A]->Init(InDataFacade))
		{
			return false;
		}
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryRangeBasedPickerOpBase::CreateScratchForScope(int32 MaxPointsInScope) const
{
	return MakeShared<FPCGExRangeBasedScratch>();
}

#pragma endregion

#pragma region FPCGExEntryRangeWeightedRandomPickerOp

int32 FPCGExEntryRangeWeightedRandomPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;
	S.Matches.Reset();
	S.Cumulative.Reset();

	ReadPointValues(PointIndex, S.PointValues);

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	const TArray<double>& EntryWeights = Shared->EntryWeights;

	// Scan valid entries; accumulate cumulative weights for entries that pass all axes.
	double TotalWeight = 0.0;

	for (const int32 i : ValidEntryIndices)
	{
		if (!MatchesAllAxes(i, S.PointValues.GetData()))
		{
			continue;
		}
		TotalWeight += EntryWeights[i];
		S.Matches.Add(i);
		S.Cumulative.Add(TotalWeight);
	}

	const int32 k = PCGExCollections::Selectors::RollCumulativeWeighted(MakeArrayView(S.Cumulative), TotalWeight, Seed);
	return k == INDEX_NONE ? -1 : Target->Indices[S.Matches[k]];
}

int32 FPCGExEntryRangeWeightedRandomPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;
	S.Matches.Reset();
	S.Cumulative.Reset();

	ReadPointValues(PointIndex, S.PointValues);

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	const TArray<double>& EntryWeights = Shared->EntryWeights;

	double TotalWeight = 0.0;
	for (const int32 i : ValidEntryIndices)
	{
		if (!InAvailability.IsAvailable(i) || !MatchesAllAxes(i, S.PointValues.GetData()))
		{
			continue;
		}
		TotalWeight += EntryWeights[i];
		S.Matches.Add(i);
		S.Cumulative.Add(TotalWeight);
	}

	const int32 k = PCGExCollections::Selectors::RollCumulativeWeighted(MakeArrayView(S.Cumulative), TotalWeight, Seed);
	return k == INDEX_NONE ? -1 : Target->Indices[S.Matches[k]];
}

#pragma endregion

#pragma region FPCGExEntryRangeFirstMatchPickerOp

int32 FPCGExEntryRangeFirstMatchPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;

	ReadPointValues(PointIndex, S.PointValues);

	// ValidEntryIndices is built in ascending entry order, so iterating it preserves
	// "first in entry order" semantics while skipping unresolved entries.
	for (const int32 i : Shared->ValidEntryIndices)
	{
		if (MatchesAllAxes(i, S.PointValues.GetData()))
		{
			return Target->Indices[i];
		}
	}

	return -1;
}

int32 FPCGExEntryRangeFirstMatchPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;

	ReadPointValues(PointIndex, S.PointValues);

	// First available match in entry order -- an exhausted first match falls through to the next.
	for (const int32 i : Shared->ValidEntryIndices)
	{
		if (InAvailability.IsAvailable(i) && MatchesAllAxes(i, S.PointValues.GetData()))
		{
			return Target->Indices[i];
		}
	}

	return -1;
}

#pragma endregion

#pragma region FPCGExEntryRangeNarrowestPickerOp

int32 FPCGExEntryRangeNarrowestPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const int32 AxisCount = Shared->AxisCount;

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;
	S.TieBucket.Reset();

	ReadPointValues(PointIndex, S.PointValues);

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	const TArray<double>& EntryMins = Shared->EntryMins;
	const TArray<double>& EntryMaxs = Shared->EntryMaxs;
	const TArray<double>& EntryWeights = Shared->EntryWeights;

	// Hypervolume = product of per-axis widths. For AxisCount=1 this is just (Max - Min), so
	// behavior is identical to the original single-axis Narrowest. For AxisCount>1 this picks
	// the most specific range region -- the geometric analogue of "narrowest".
	double MinHypervolume = TNumericLimits<double>::Max();
	double TieTolerance = 0.0;

	for (const int32 i : ValidEntryIndices)
	{
		if (!MatchesAllAxes(i, S.PointValues.GetData()))
		{
			continue;
		}

		const int32 Base = i * AxisCount;
		double Hypervolume = 1.0;
		for (int32 A = 0; A < AxisCount; ++A)
		{
			Hypervolume *= (EntryMaxs[Base + A] - EntryMins[Base + A]);
		}

		if (Hypervolume < MinHypervolume - TieTolerance)
		{
			MinHypervolume = Hypervolume;
			TieTolerance = PCGExSelectorRangeBased::RangeTieTolerance(MinHypervolume);
			S.TieBucket.Reset();
			S.TieBucket.Add(i);
		}
		else if (FMath::Abs(Hypervolume - MinHypervolume) <= TieTolerance)
		{
			S.TieBucket.Add(i);
		}
	}

	if (S.TieBucket.IsEmpty())
	{
		return -1;
	}
	if (S.TieBucket.Num() == 1)
	{
		return Target->Indices[S.TieBucket[0]];
	}

	// Tie-break by weight via streaming roll (no need to materialize a cumulative array).
	double TotalWeight = 0.0;
	for (const int32 i : S.TieBucket)
	{
		TotalWeight += EntryWeights[i];
	}
	if (TotalWeight <= 0.0)
	{
		return Target->Indices[S.TieBucket[0]];
	}

	const int32 k = PCGExCollections::Selectors::RollWeightedStreaming(
		S.TieBucket.Num(),
		[&](int32 LocalIdx)
		{
			return EntryWeights[S.TieBucket[LocalIdx]];
		},
		TotalWeight, Seed);
	return Target->Indices[S.TieBucket[k]];
}

int32 FPCGExEntryRangeNarrowestPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const int32 AxisCount = Shared->AxisCount;

	FPCGExRangeBasedScratch LocalScratch;
	FPCGExRangeBasedScratch& S = Scratch ? *static_cast<FPCGExRangeBasedScratch*>(Scratch) : LocalScratch;
	S.TieBucket.Reset();

	ReadPointValues(PointIndex, S.PointValues);

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	const TArray<double>& EntryMins = Shared->EntryMins;
	const TArray<double>& EntryMaxs = Shared->EntryMaxs;
	const TArray<double>& EntryWeights = Shared->EntryWeights;

	// Same narrowest-wins scan as Pick with unavailable entries excluded up front -- an
	// exhausted narrowest falls through to the next-narrowest available.
	double MinHypervolume = TNumericLimits<double>::Max();
	double TieTolerance = 0.0;

	for (const int32 i : ValidEntryIndices)
	{
		if (!InAvailability.IsAvailable(i) || !MatchesAllAxes(i, S.PointValues.GetData()))
		{
			continue;
		}

		const int32 Base = i * AxisCount;
		double Hypervolume = 1.0;
		for (int32 A = 0; A < AxisCount; ++A)
		{
			Hypervolume *= (EntryMaxs[Base + A] - EntryMins[Base + A]);
		}

		if (Hypervolume < MinHypervolume - TieTolerance)
		{
			MinHypervolume = Hypervolume;
			TieTolerance = PCGExSelectorRangeBased::RangeTieTolerance(MinHypervolume);
			S.TieBucket.Reset();
			S.TieBucket.Add(i);
		}
		else if (FMath::Abs(Hypervolume - MinHypervolume) <= TieTolerance)
		{
			S.TieBucket.Add(i);
		}
	}

	if (S.TieBucket.IsEmpty())
	{
		return -1;
	}
	if (S.TieBucket.Num() == 1)
	{
		return Target->Indices[S.TieBucket[0]];
	}

	double TotalWeight = 0.0;
	for (const int32 i : S.TieBucket)
	{
		TotalWeight += EntryWeights[i];
	}
	if (TotalWeight <= 0.0)
	{
		return Target->Indices[S.TieBucket[0]];
	}

	const int32 k = PCGExCollections::Selectors::RollWeightedStreaming(
		S.TieBucket.Num(),
		[&](int32 LocalIdx)
		{
			return EntryWeights[S.TieBucket[LocalIdx]];
		},
		TotalWeight, Seed);
	return Target->Indices[S.TieBucket[k]];
}

#pragma endregion

#pragma region UPCGExSelectorRangeBasedFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorRangeBasedFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryRangeBasedPickerOpBase> NewOp;

	switch (Config.OverlapMode)
	{
	default:
	case EPCGExRangeOverlapMode::WeightedRandom:
		NewOp = MakeShared<FPCGExEntryRangeWeightedRandomPickerOp>();
		break;
	case EPCGExRangeOverlapMode::FirstMatch:
		NewOp = MakeShared<FPCGExEntryRangeFirstMatchPickerOp>();
		break;
	case EPCGExRangeOverlapMode::NarrowestWins:
		NewOp = MakeShared<FPCGExEntryRangeNarrowestPickerOp>();
		break;
	}

	// Per-axis ValueSource drives per-facade getters; per-entry property names are consumed
	// only by BuildSharedData. Copying Axes wholesale keeps the op self-contained for getter init.
	NewOp->Axes = Config.Axes;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorRangeBasedFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target)
	{
		return nullptr;
	}

	const int32 AxisCount = Config.Axes.Num();
	if (AxisCount == 0)
	{
		return nullptr;
	}

	const int32 N = Target->Entries.Num();
	if (N == 0)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExRangeBasedSharedData> NewShared = MakeShared<FPCGExRangeBasedSharedData>();
	NewShared->AxisCount = AxisCount;
	NewShared->EntryCount = N;
	NewShared->EntryMins.SetNumUninitialized(N * AxisCount);
	NewShared->EntryMaxs.SetNumUninitialized(N * AxisCount);
	NewShared->EntryWeights.SetNumZeroed(N);

	NewShared->AxisBoundaryModes.SetNumUninitialized(AxisCount);
	for (int32 A = 0; A < AxisCount; ++A)
	{
		NewShared->AxisBoundaryModes[A] = Config.Axes[A].BoundaryMode;
	}

	// Entries are valid only when every axis resolved; partial resolution => entry excluded entirely.
	// Excluded entries get a sentinel range (Min=1, Max=-1) per axis so MatchesAllAxes can never accept them.
	auto MarkInvalid = [&](int32 Base)
	{
		for (int32 A = 0; A < AxisCount; ++A)
		{
			NewShared->EntryMins[Base + A] = 1.0;
			NewShared->EntryMaxs[Base + A] = -1.0;
		}
	};

	NewShared->ValidEntryIndices.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FPCGExAssetCollectionEntry* Entry = Target->Entries[i];
		const int32 Base = i * AxisCount;

		if (!Entry)
		{
			MarkInvalid(Base);
			continue;
		}

		bool bAllResolved = true;
		for (int32 A = 0; A < AxisCount; ++A)
		{
			double Min = 0.0;
			double Max = 0.0;
			if (!PCGExSelectorRangeBased::ResolveAxisForEntry(Config.Axes[A], Entry, Collection, Min, Max))
			{
				bAllResolved = false;
				break;
			}
			NewShared->EntryMins[Base + A] = Min;
			NewShared->EntryMaxs[Base + A] = Max;
		}

		if (!bAllResolved)
		{
			MarkInvalid(Base);
			continue;
		}

		NewShared->EntryWeights[i] = PCGExCollections::Selectors::EntryEffectiveWeight(Entry);
		NewShared->ValidEntryIndices.Add(i);
	}

	// Caller (op's PrepareForData → OnSharedDataMissing) reports the error when Shared is null.
	if (NewShared->ValidEntryIndices.IsEmpty())
	{
		return nullptr;
	}

	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorRangeBasedFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorRangeBasedFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExSelectorRangeBasedFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorRangeBasedFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorRangeBasedFactoryProviderSettings::GetDisplayName() const
{
	switch (Config.OverlapMode)
	{
	default:
	case EPCGExRangeOverlapMode::WeightedRandom:
		return TEXT("Select : Range Weighted");
	case EPCGExRangeOverlapMode::FirstMatch:
		return TEXT("Select : Range First");
	case EPCGExRangeOverlapMode::NarrowestWins:
		return TEXT("Select : Range Narrowest");
	}
}
#endif

#pragma endregion
