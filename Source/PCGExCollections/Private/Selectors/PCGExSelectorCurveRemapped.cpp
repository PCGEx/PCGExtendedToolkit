// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorCurveRemapped.h"

#include "PCGExPropertyTypes.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Selectors/PCGExSelectorHelpers.h"

#pragma region FPCGExEntryCurveRemappedPickerOp

void FPCGExEntryCurveRemappedPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Curve-Remapped Weight -- no entry resolved the configured Float Curve property. Check the property name and that entries (or the collection) author a curve."))
}

bool FPCGExEntryCurveRemappedPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	TimeGetter = TimeSource.GetValueSetting();
	if (!TimeGetter->Init(InDataFacade))
	{
		return false;
	}

	// Constant-t fast path: the effective-weight table is point-invariant, so build the
	// cumulative once here and reduce Pick to a single roll.
	if (TimeGetter->IsConstant())
	{
		bConstantTime = true;

		const double Time = TimeGetter->Read(0);
		const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
		const int32 N = ValidEntryIndices.Num();

		ConstantCumulative.SetNumUninitialized(N);
		double Total = 0.0;
		for (int32 k = 0; k < N; ++k)
		{
			Total += EffectiveWeight(ValidEntryIndices[k], Time);
			ConstantCumulative[k] = Total;
		}
		ConstantTotalWeight = Total;
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryCurveRemappedPickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	if (bConstantTime)
	{
		return nullptr;
	}
	return MakeShared<FPCGExCurveRemappedScratch>();
}

int32 FPCGExEntryCurveRemappedPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(TimeGetter);

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;

	if (bConstantTime)
	{
		const int32 k = PCGExCollections::Selectors::RollCumulativeWeighted(MakeArrayView(ConstantCumulative), ConstantTotalWeight, Seed);
		return k == INDEX_NONE ? -1 : Target->Indices[ValidEntryIndices[k]];
	}

	const double Time = TimeGetter->Read(PointIndex);
	const int32 N = ValidEntryIndices.Num();

	FPCGExCurveRemappedScratch LocalScratch;
	FPCGExCurveRemappedScratch& S = Scratch ? *static_cast<FPCGExCurveRemappedScratch*>(Scratch) : LocalScratch;
	S.EffectiveWeights.SetNumUninitialized(N, EAllowShrinking::No);

	double TotalWeight = 0.0;
	for (int32 k = 0; k < N; ++k)
	{
		S.EffectiveWeights[k] = EffectiveWeight(ValidEntryIndices[k], Time);
		TotalWeight += S.EffectiveWeights[k];
	}

	// All curves gated out at this t -> no pick (streaming roll returns INDEX_NONE on zero total).
	const int32 k = PCGExCollections::Selectors::RollWeightedStreaming(
		N,
		[&S](int32 LocalIdx)
		{
			return S.EffectiveWeights[LocalIdx];
		},
		TotalWeight, Seed);
	return k == INDEX_NONE ? -1 : Target->Indices[ValidEntryIndices[k]];
}

int32 FPCGExEntryCurveRemappedPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(TimeGetter);

	// Quota-only path: constant-t precompute bypassed (availability varies per pick); two-pass
	// streaming with unavailable entries contributing nothing and skipped in the walk.
	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	const double Time = TimeGetter->Read(PointIndex);
	const int32 N = ValidEntryIndices.Num();

	double TotalWeight = 0.0;
	for (int32 k = 0; k < N; ++k)
	{
		if (InAvailability.IsAvailable(ValidEntryIndices[k]))
		{
			TotalWeight += EffectiveWeight(ValidEntryIndices[k], Time);
		}
	}
	if (TotalWeight <= 0.0)
	{
		return -1;
	}

	const double Roll = FRandomStream(Seed).FRandRange(0.0, TotalWeight);
	double Acc = 0.0;
	int32 LastPickable = -1;
	for (int32 k = 0; k < N; ++k)
	{
		const int32 EntryIndex = ValidEntryIndices[k];
		if (!InAvailability.IsAvailable(EntryIndex))
		{
			continue;
		}
		const double W = EffectiveWeight(EntryIndex, Time);
		if (W <= 0.0)
		{
			continue;
		}
		LastPickable = EntryIndex;
		Acc += W;
		if (Roll <= Acc)
		{
			return Target->Indices[EntryIndex];
		}
	}
	return LastPickable == -1 ? -1 : Target->Indices[LastPickable];
}

#pragma endregion

#pragma region UPCGExSelectorCurveRemappedFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorCurveRemappedFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryCurveRemappedPickerOp> NewOp = MakeShared<FPCGExEntryCurveRemappedPickerOp>();
	NewOp->bMultiplyByEntryWeight = Config.bMultiplyByEntryWeight;
	NewOp->TimeSource = Config.TimeSource;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorCurveRemappedFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target || Config.CurvePropertyName.IsNone())
	{
		return nullptr;
	}

	const int32 N = Target->Entries.Num();
	if (N == 0)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExCurveRemappedSharedData> NewShared = MakeShared<FPCGExCurveRemappedSharedData>();
	NewShared->EntryLookups.SetNum(N);
	NewShared->EntryWeights.SetNumUninitialized(N);
	NewShared->ValidEntryIndices.Reserve(N);

	for (int32 i = 0; i < N; ++i)
	{
		const FPCGExAssetCollectionEntry* Entry = Target->Entries[i];
		NewShared->EntryWeights[i] = PCGExCollections::Selectors::EntryEffectiveWeight(Entry);

		// Entry override first, collection default second. FRuntimeFloatCurve is inline data --
		// no asset loading involved, safe to bake from any thread.
		const FPCGExProperty_FloatCurve* CurveProp = Entry->GetResolvedProperty<FPCGExProperty_FloatCurve>(Collection, Config.CurvePropertyName);
		if (!CurveProp)
		{
			continue;
		}

		PCGExFloatLUT Lookup = Config.CurveLookup.MakeFloatLookup(CurveProp->Value);
		if (!Lookup || !Lookup->IsValid())
		{
			continue;
		}

		NewShared->EntryLookups[i] = Lookup;
		NewShared->ValidEntryIndices.Add(i);
	}

	// Caller (op's PrepareForData -> OnSharedDataMissing) reports the error when Shared is null.
	if (NewShared->ValidEntryIndices.IsEmpty())
	{
		return nullptr;
	}

	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorCurveRemappedFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorCurveRemappedFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExSelectorCurveRemappedFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorCurveRemappedFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorCurveRemappedFactoryProviderSettings::GetDisplayName() const
{
	return Config.CurvePropertyName.IsNone()
		? TEXT("Select : Curve-Remapped")
		: TEXT("Select : Curve ") + Config.CurvePropertyName.ToString();
}
#endif

#pragma endregion
