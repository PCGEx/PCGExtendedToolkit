// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorDensityWeighted.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Selectors/PCGExSelectorHelpers.h"

#pragma region FPCGExEntryDensityWeightedPickerOp

void FPCGExEntryDensityWeightedPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Density-Weighted -- failed to build shared weight tables. Check that the target category has at least one valid entry."))
}

bool FPCGExEntryDensityWeightedPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	DensityGetter = DensitySource.GetValueSetting();
	if (!DensityGetter->Init(InDataFacade))
	{
		return false;
	}

	// Constant-density fast path: the effective-weight table is point-invariant, so build the
	// cumulative once here and reduce Pick to a single roll.
	if (DensityGetter->IsConstant())
	{
		bConstantDensity = true;

		double Density = DensityGetter->Read(0);
		if (Density < 0.0 || Density > 1.0)
		{
			if (OutOfRangePolicy == EPCGExDensityOutOfRangePolicy::SkipPoint)
			{
				bSkipAllPoints = true;
				return true;
			}
			Density = FMath::Clamp(Density, 0.0, 1.0);
		}

		const TArray<double>& EntryWeights = Shared->EntryWeights;
		const TArray<double>& EntryLogWeights = Shared->EntryLogWeights;
		const int32 N = EntryWeights.Num();

		ConstantCumulative.SetNumUninitialized(N);
		double Total = 0.0;

		switch (Mode)
		{
		default:
		case EPCGExDensityWeightMode::WeightModulation:
		{
			const double Exponent = FMath::Lerp(1.0, Density * 2.0, DensityInfluence);
			for (int32 i = 0; i < N; ++i)
			{
				Total += FMath::Exp(EntryLogWeights[i] * Exponent);
				ConstantCumulative[i] = Total;
			}
			break;
		}
		case EPCGExDensityWeightMode::RandomnessModulation:
		{
			const double EffectiveDensity = (1.0 - DensityInfluence) + DensityInfluence * Density;
			for (int32 i = 0; i < N; ++i)
			{
				Total += FMath::Lerp(1.0, EntryWeights[i], EffectiveDensity);
				ConstantCumulative[i] = Total;
			}
			break;
		}
		}

		ConstantTotalWeight = Total;
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryDensityWeightedPickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	// Only the per-point WeightModulation path materializes an effective-weight array.
	if (bConstantDensity || Mode != EPCGExDensityWeightMode::WeightModulation)
	{
		return nullptr;
	}
	return MakeShared<FPCGExDensityWeightedScratch>();
}

int32 FPCGExEntryDensityWeightedPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(DensityGetter);

	const TArray<double>& EntryWeights = Shared->EntryWeights;

	if (bConstantDensity)
	{
		if (bSkipAllPoints)
		{
			return -1;
		}
		const int32 Pick = PCGExCollections::Selectors::RollCumulativeWeighted(MakeArrayView(ConstantCumulative), ConstantTotalWeight, Seed);
		return Pick == INDEX_NONE ? -1 : Target->Indices[Pick];
	}

	double Density = DensityGetter->Read(PointIndex);

	// Out-of-range policy: either clamp, or skip the point entirely.
	if (Density < 0.0 || Density > 1.0)
	{
		if (OutOfRangePolicy == EPCGExDensityOutOfRangePolicy::SkipPoint)
		{
			return -1;
		}
		Density = FMath::Clamp(Density, 0.0, 1.0);
	}

	const int32 N = EntryWeights.Num();

	switch (Mode)
	{
	default:
	case EPCGExDensityWeightMode::WeightModulation:
	{
		// exponent = lerp(1, density*2, DensityInfluence)
		// DI=0               -> exp=1               -> plain weighted (parity with WeightedRandom)
		// DI=1 & density=0   -> exp=0               -> uniform (all weights become 1)
		// DI=1 & density=0.5 -> exp=1               -> plain weighted
		// DI=1 & density=1   -> exp=2               -> amplified bias toward higher weights
		// Pow(W, Exp) rewritten as exp(LogW * Exp). Shared data hosts LogW once per category --
		// per-pick cost drops from one log+one exp to one exp, with no precision change for W >= 0.
		const double Exponent = FMath::Lerp(1.0, Density * 2.0, DensityInfluence);

		// Exponent == 1 is exact at the two most common operating points (DI=0, or density=0.5
		// at full influence) and reduces to a plain weighted roll -- skip the N transcendentals.
		if (Exponent == 1.0)
		{
			const int32 Pick = PCGExCollections::Selectors::RollWeightedStreaming(
				N,
				[&EntryWeights](int32 LocalIdx)
				{
					return EntryWeights[LocalIdx];
				},
				Shared->TotalWeight, Seed);
			return Pick == INDEX_NONE ? -1 : Target->Indices[Pick];
		}

		const TArray<double>& EntryLogWeights = Shared->EntryLogWeights;

		FPCGExDensityWeightedScratch LocalScratch;
		FPCGExDensityWeightedScratch& S = Scratch ? *static_cast<FPCGExDensityWeightedScratch*>(Scratch) : LocalScratch;
		S.EffectiveWeights.SetNumUninitialized(N, EAllowShrinking::No);

		double TotalWeight = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			S.EffectiveWeights[i] = FMath::Exp(EntryLogWeights[i] * Exponent);
			TotalWeight += S.EffectiveWeights[i];
		}

		const int32 Pick = PCGExCollections::Selectors::RollWeightedStreaming(
			N,
			[&S](int32 LocalIdx)
			{
				return S.EffectiveWeights[LocalIdx];
			},
			TotalWeight, Seed);
		return Pick == INDEX_NONE ? -1 : Target->Indices[Pick];
	}
	case EPCGExDensityWeightMode::RandomnessModulation:
	{
		// effective_density = (1 - DI) + DI * density, then effective_weight = lerp(1, W, effective_density)
		// DI=0               -> eff=1               -> plain weighted (parity with WeightedRandom)
		// DI=1 & density=1   -> eff=1               -> plain weighted
		// DI=1 & density=0   -> eff=0               -> uniform
		// The total is analytic -- sum of lerp(1, W, e) = N*(1-e) + e*SumW -- so no per-entry
		// array is materialized; the streaming roll computes each weight on the fly.
		const double EffectiveDensity = (1.0 - DensityInfluence) + DensityInfluence * Density;
		const double TotalWeight = N * (1.0 - EffectiveDensity) + EffectiveDensity * Shared->TotalWeight;

		const int32 Pick = PCGExCollections::Selectors::RollWeightedStreaming(
			N,
			[&EntryWeights, EffectiveDensity](int32 LocalIdx)
			{
				return FMath::Lerp(1.0, EntryWeights[LocalIdx], EffectiveDensity);
			},
			TotalWeight, Seed);
		return Pick == INDEX_NONE ? -1 : Target->Indices[Pick];
	}
	}
}

int32 FPCGExEntryDensityWeightedPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(DensityGetter);

	// Quota-only path: the constant-density precomputed cumulative can't be used (availability
	// varies per pick), so both modes stream with the availability guard. Reading the getter is
	// valid for constant sources too.
	if (bSkipAllPoints)
	{
		return -1;
	}

	double Density = DensityGetter->Read(PointIndex);
	if (Density < 0.0 || Density > 1.0)
	{
		if (OutOfRangePolicy == EPCGExDensityOutOfRangePolicy::SkipPoint)
		{
			return -1;
		}
		Density = FMath::Clamp(Density, 0.0, 1.0);
	}

	const TArray<double>& EntryWeights = Shared->EntryWeights;
	const TArray<double>& EntryLogWeights = Shared->EntryLogWeights;
	const int32 N = EntryWeights.Num();

	const double Exponent = FMath::Lerp(1.0, Density * 2.0, DensityInfluence);
	const double EffectiveDensity = (1.0 - DensityInfluence) + DensityInfluence * Density;

	auto WeightOf = [&](const int32 i) -> double
	{
		return Mode == EPCGExDensityWeightMode::WeightModulation
			? FMath::Exp(EntryLogWeights[i] * Exponent)
			: FMath::Lerp(1.0, EntryWeights[i], EffectiveDensity);
	};

	double TotalWeight = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		if (InAvailability.IsAvailable(i))
		{
			TotalWeight += WeightOf(i);
		}
	}
	if (TotalWeight <= 0.0)
	{
		return -1;
	}

	const double Roll = FRandomStream(Seed).FRandRange(0.0, TotalWeight);
	double Acc = 0.0;
	int32 LastAvailable = -1;
	for (int32 i = 0; i < N; ++i)
	{
		if (!InAvailability.IsAvailable(i))
		{
			continue;
		}
		LastAvailable = i;
		Acc += WeightOf(i);
		if (Roll <= Acc)
		{
			return Target->Indices[i];
		}
	}
	return LastAvailable == -1 ? -1 : Target->Indices[LastAvailable];
}

#pragma endregion

#pragma region UPCGExSelectorDensityWeightedFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorDensityWeightedFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryDensityWeightedPickerOp> NewOp = MakeShared<FPCGExEntryDensityWeightedPickerOp>();
	NewOp->Mode = Config.Mode;
	// Property clamp metadata doesn't guard values injected through overrides -- re-clamp at
	// init so the hot path can rely on the invariant (out-of-range influence flips the
	// WeightModulation exponent negative and inverts the weight preference).
	NewOp->DensityInfluence = FMath::Clamp(Config.DensityInfluence, 0.0, 1.0);
	NewOp->OutOfRangePolicy = Config.OutOfRangePolicy;
	NewOp->DensitySource = Config.DensitySource;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorDensityWeightedFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target)
	{
		return nullptr;
	}

	const int32 N = Target->Entries.Num();
	if (N == 0)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExDensityWeightedSharedData> NewShared = MakeShared<FPCGExDensityWeightedSharedData>();
	NewShared->EntryWeights.SetNumUninitialized(N);
	NewShared->EntryLogWeights.SetNumUninitialized(N);

	double TotalWeight = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		const double W = PCGExCollections::Selectors::EntryEffectiveWeight(Target->Entries[i]);
		NewShared->EntryWeights[i] = W;
		// EntryEffectiveWeight returns Weight + 1 and category entries are never null
		// (FCache::RegisterEntry asserts), so W >= 1 and log is always well-defined; the
		// defensive fallback exists only to keep Exp() finite if that invariant ever breaks.
		NewShared->EntryLogWeights[i] = W > 0.0 ? FMath::Loge(W) : 0.0;
		TotalWeight += W;
	}
	NewShared->TotalWeight = TotalWeight;

	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorDensityWeightedFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorDensityWeightedFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExSelectorDensityWeightedFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorDensityWeightedFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorDensityWeightedFactoryProviderSettings::GetDisplayName() const
{
	switch (Config.Mode)
	{
	default:
	case EPCGExDensityWeightMode::WeightModulation:
		return TEXT("Select : Weight Mod");
	case EPCGExDensityWeightMode::RandomnessModulation:
		return TEXT("Select : Randomness Mod");
	}
}
#endif

#pragma endregion
