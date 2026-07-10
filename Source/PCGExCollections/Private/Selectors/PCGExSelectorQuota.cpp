// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorQuota.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Selectors/PCGExSelectorCascade.h"

namespace PCGExSelectorQuota
{
	constexpr double GoldenRatioConjugate = 0.6180339887498948482;
}

#pragma region FPCGExQuotaSharedData

void FPCGExQuotaSharedData::OnCached(const PCGExCollections::FSelectorSharedDataCache& InCache)
{
	// Finalize the AllInputs Proportion budget against the batch total: per-facade accumulation
	// let early facades pick against a partial cap. Runs under the cache lock, before any op
	// can observe this instance.
	if (bProportionBudget && SharedRemaining && InCache.AllInputsPointCount >= 0)
	{
		const double TotalD = static_cast<double>(InCache.AllInputsPointCount);
		const int32 N = MaxValues.Num();
		for (int32 i = 0; i < N; ++i)
		{
			if (MaxValues[i] >= 0.0)
			{
				SharedRemaining[i].store(FMath::CeilToInt32(FMath::Clamp(MaxValues[i], 0.0, 1.0) * TotalD), std::memory_order_relaxed);
			}
		}
		bBudgetFinalized = true;
	}

	if (ChildSharedData)
	{
		ChildSharedData->OnCached(InCache);
	}
}

#pragma endregion

#pragma region FPCGExEntryQuotaPickerOp

void FPCGExEntryQuotaPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector Modifier : Quota -- failed to build shared quota tables. Check the quota property names and that the target category has valid entries."))
}

int32 FPCGExEntryQuotaPickerOp::ResolveCount(const double RawValue) const
{
	return Mode == EPCGExQuotaMode::Proportion
		? FMath::CeilToInt32(FMath::Clamp(RawValue, 0.0, 1.0) * NumPointsD)
		: FMath::RoundToInt32(RawValue);
}

bool FPCGExEntryQuotaPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	const UPCGBasePointData* PointData = InDataFacade->Source->GetIn();
	if (!PointData)
	{
		return false;
	}

	NumPointsD = PointData->GetNumPoints();
	NumEntries = Shared->MaxValues.Num();

	// Raw -> category-local mapping (inner selectors return raw indices; counters are local).
	int32 MaxRaw = -1;
	for (const int32 Raw : Target->Indices)
	{
		MaxRaw = FMath::Max(MaxRaw, Raw);
	}
	RawToLocal.Init(-1, MaxRaw + 1);
	for (int32 i = 0; i < Target->Indices.Num(); ++i)
	{
		RawToLocal[Target->Indices[i]] = i;
	}

	// --- Max caps: resolve the effective counter block per scope ---
	if (Scope == EPCGExQuotaScope::AllInputs && Shared->SharedRemaining)
	{
		Remaining = Shared->SharedRemaining.Get();
		if (Mode == EPCGExQuotaMode::Proportion && !Shared->bBudgetFinalized)
		{
			// Fallback when OnCached didn't finalize (cache bypassed / no batch total): accumulate
			// per facade. Early facades may transiently see a partial cap -- wired consumers
			// (Distribute, Spline Mesh) avoid this by setting AllInputsPointCount on the cache.
			for (int32 i = 0; i < NumEntries; ++i)
			{
				if (Shared->MaxValues[i] >= 0.0)
				{
					Remaining[i].fetch_add(ResolveCount(Shared->MaxValues[i]), std::memory_order_relaxed);
				}
			}
		}
	}
	else
	{
		LocalRemaining = MakeUnique<std::atomic<int32>[]>(NumEntries);
		for (int32 i = 0; i < NumEntries; ++i)
		{
			const double MaxValue = Shared->MaxValues[i];
			LocalRemaining[i].store(MaxValue >= 0.0 ? ResolveCount(MaxValue) : -1, std::memory_order_relaxed);
		}
		Remaining = LocalRemaining.Get();
	}

	// --- Min reservation tables (always per facade -- reservation is point-order-local) ---
	MinLocalEntries.Reset();
	MinCumulativeCounts.Reset();
	TotalMinCount = 0.0;
	for (int32 i = 0; i < NumEntries; ++i)
	{
		const double MinValue = Shared->MinValues[i];
		if (MinValue <= 0.0)
		{
			continue;
		}
		const int32 MinCount = ResolveCount(MinValue);
		if (MinCount <= 0)
		{
			continue;
		}
		TotalMinCount += MinCount;
		MinLocalEntries.Add(i);
		MinCumulativeCounts.Add(TotalMinCount);
	}
	ReservationFraction = NumPointsD > 0.0 ? FMath::Min(1.0, TotalMinCount / NumPointsD) : 0.0;

	// --- Inner selector ---
	if (ChildFactory)
	{
		ChildOp = ChildFactory->CreateEntryOperation(InContext);
		if (ChildOp)
		{
			if (const TSharedPtr<FPCGExCascadeSharedData> Composite = StaticCastSharedPtr<FPCGExCascadeSharedData>(Shared->ChildSharedData);
				Composite && Composite->PerChild.Num() == 1)
			{
				ChildOp->SharedData = Composite->PerChild[0];
			}
			if (!ChildOp->PrepareForData(InContext, InDataFacade, Target, OwningCollection))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Selector Modifier : Quota -- inner selector failed to initialize; falling back to weighted random."));
				ChildOp = nullptr;
			}
		}
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryQuotaPickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	TSharedPtr<FPCGExPickerScratchBase> ChildScratch = ChildOp ? ChildOp->CreateScratchForScope(MaxPointsInScope) : nullptr;
	if (!ChildScratch)
	{
		return nullptr;
	}
	TSharedPtr<FPCGExQuotaScratch> Scratch = MakeShared<FPCGExQuotaScratch>();
	Scratch->ChildScratch = ChildScratch;
	return Scratch;
}

int32 FPCGExEntryQuotaPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(Remaining);

	FPCGExPickerScratchBase* ChildScratch = Scratch ? static_cast<FPCGExQuotaScratch*>(Scratch)->ChildScratch.Get() : nullptr;
	const FPCGExPickAvailability Availability{Remaining};

	// --- Min reservation: a deterministic, evenly-spread fraction of points is force-assigned
	// to under-minimum entries before the inner selector runs (±1 accurate per entry). ---
	if (ReservationFraction > 0.0)
	{
		const double U = FMath::Frac((PointIndex + 0.5) * PCGExSelectorQuota::GoldenRatioConjugate + ReservationPhase);
		if (U < ReservationFraction)
		{
			// Map the reserved slot through the cumulative min counts.
			const double Slot = (U / ReservationFraction) * TotalMinCount;
			const int32 k = FMath::Min(Algo::LowerBound(MinCumulativeCounts, Slot), MinCumulativeCounts.Num() - 1);
			const int32 Local = MinLocalEntries[k];

			// Reservations honor max caps: claim capacity like any pick. On conflict (authored
			// min > max, or capacity raced away) fall through to the normal path.
			int32 Current = Remaining[Local].load(std::memory_order_relaxed);
			if (Current < 0)
			{
				return Target->Indices[Local];
			}
			while (Current > 0)
			{
				if (Remaining[Local].compare_exchange_weak(Current, Current - 1, std::memory_order_relaxed))
				{
					return Target->Indices[Local];
				}
			}
		}
	}

	// --- Normal path: filtered inner pick + CAS claim. Bounded: every lost claim race
	// permanently exhausts one entry, so NumEntries + 1 attempts always suffice. ---
	for (int32 Attempt = 0; Attempt <= NumEntries; ++Attempt)
	{
		const int32 Raw = ChildOp
			? ChildOp->PickFiltered(PointIndex, Seed, Availability, ChildScratch)
			: PCGExCollections::Selectors::FilteredWeightedPick(Target, Availability, Seed);

		if (Raw == -1)
		{
			break; // Inner reports nothing available -> exhausted.
		}

		const int32 Local = RawToLocal.IsValidIndex(Raw) ? RawToLocal[Raw] : INDEX_NONE;
		if (Local == INDEX_NONE)
		{
			break; // Defensive: inner returned an entry outside the bound category.
		}

		int32 Current = Remaining[Local].load(std::memory_order_relaxed);
		if (Current < 0)
		{
			return Raw; // Uncapped -- no claim needed.
		}
		while (Current > 0)
		{
			if (Remaining[Local].compare_exchange_weak(Current, Current - 1, std::memory_order_relaxed))
			{
				return Raw;
			}
		}
		// Claim race lost and the entry is now exhausted -- the availability view already
		// reflects it; re-invoke the inner pick.
	}

	if (ExhaustedBehavior == EPCGExQuotaExhaustedBehavior::IgnoreQuota)
	{
		return ChildOp ? ChildOp->Pick(PointIndex, Seed, ChildScratch) : Target->GetPickRandomWeighted(Seed);
	}
	return -1;
}

#pragma endregion

#pragma region UPCGExSelectorQuotaFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorQuotaFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryQuotaPickerOp> NewOp = MakeShared<FPCGExEntryQuotaPickerOp>();
	NewOp->Mode = Config.Mode;
	NewOp->Scope = Config.Scope;
	NewOp->ExhaustedBehavior = Config.ExhaustedBehavior;
	NewOp->ReservationPhase = FMath::Frac(BaseConfig.LocalSeed * PCGExSelectorQuota::GoldenRatioConjugate);
	NewOp->ChildFactory = Child.Get();
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorQuotaFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target || (Config.MaxPropertyName.IsNone() && Config.MinPropertyName.IsNone()))
	{
		return nullptr;
	}

	const int32 N = Target->Entries.Num();
	if (N == 0)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExQuotaSharedData> NewShared = MakeShared<FPCGExQuotaSharedData>();
	NewShared->EntryWeights.SetNumUninitialized(N);
	NewShared->MaxValues.SetNumUninitialized(N);
	NewShared->MinValues.SetNumUninitialized(N);

	for (int32 i = 0; i < N; ++i)
	{
		const FPCGExAssetCollectionEntry* Entry = Target->Entries[i];
		NewShared->EntryWeights[i] = PCGExCollections::Selectors::EntryEffectiveWeight(Entry);

		double MaxValue = -1.0; // unresolved = uncapped
		if (!Config.MaxPropertyName.IsNone())
		{
			Entry->TryGetPropertyValue<double>(Collection, Config.MaxPropertyName, MaxValue);
		}
		NewShared->MaxValues[i] = MaxValue;

		double MinValue = 0.0; // unresolved = no minimum
		if (!Config.MinPropertyName.IsNone())
		{
			Entry->TryGetPropertyValue<double>(Collection, Config.MinPropertyName, MinValue);
		}
		NewShared->MinValues[i] = MinValue;
	}

	// AllInputs scope: the counter block lives here so every facade shares it. Count mode is
	// fully initialized now; Proportion mode starts at 0 and is finalized in OnCached (with a
	// per-facade fallback at op init when no batch total was provided).
	if (Config.Scope == EPCGExQuotaScope::AllInputs)
	{
		NewShared->bProportionBudget = Config.Mode == EPCGExQuotaMode::Proportion;
		NewShared->SharedRemaining = MakeUnique<std::atomic<int32>[]>(N);
		for (int32 i = 0; i < N; ++i)
		{
			const double MaxValue = NewShared->MaxValues[i];
			int32 Initial = -1;
			if (MaxValue >= 0.0)
			{
				Initial = Config.Mode == EPCGExQuotaMode::Count ? FMath::RoundToInt32(MaxValue) : 0;
			}
			NewShared->SharedRemaining[i].store(Initial, std::memory_order_relaxed);
		}
	}

	// Child shared data rides along (single-slot composite, mirrors Anti-Repeat).
	if (const UPCGExSelectorFactoryData* ChildPtr = Child.Get())
	{
		TSharedPtr<FPCGExCascadeSharedData> Composite = MakeShared<FPCGExCascadeSharedData>();
		Composite->PerChild.SetNum(1);
		Composite->PerChild[0] = ChildPtr->BuildSharedData(Collection, Target);
		NewShared->ChildSharedData = Composite;
	}

	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorQuotaFactoryProviderSettings

TArray<FPCGPinProperties> UPCGExSelectorQuotaFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExCollections::Labels::SourceSelectorLabel, "Optional inner selector the quota wraps. When unconnected, Quota wraps a plain weighted-random pick. If multiple are connected, the lowest Priority wins.", Normal, FPCGExDataTypeInfoSelector::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExSelectorQuotaFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGExSelectorFactoryData>> Children;
	PCGExFactories::GetInputFactories(InContext, PCGExCollections::Labels::SourceSelectorLabel, Children, {FPCGExDataTypeInfoSelector::AsId()}, false);

	UPCGExSelectorQuotaFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorQuotaFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	if (!Children.IsEmpty())
	{
		if (Children.Num() > 1)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Selector Modifier : Quota -- multiple inner selectors connected; using the lowest Priority. Use Selector Modifier : Cascade to combine several."));
		}
		NewFactory->Child = Children[0];
	}
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorQuotaFactoryProviderSettings::GetDisplayName() const
{
	return TEXT("Modify : Quota");
}
#endif

#pragma endregion
