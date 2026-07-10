// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorInterleaved.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"

namespace PCGExSelectorInterleaved
{
	// Golden ratio conjugate (1/φ). Additive recurrence over this constant is the classic
	// maximally-uniform low-discrepancy sequence on [0, 1).
	constexpr double GoldenRatioConjugate = 0.6180339887498948482;
}

#pragma region FPCGExEntryInterleavedPickerOp

void FPCGExEntryInterleavedPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Interleaved -- failed to build shared weight tables. Check that the target category has at least one valid entry."))
}

bool FPCGExEntryInterleavedPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	OrdinalGetter = OrdinalSource.GetValueSetting();
	if (!OrdinalGetter->Init(InDataFacade))
	{
		return false;
	}
	return true;
}

int32 FPCGExEntryInterleavedPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(OrdinalGetter);

	// Per-point Seed is deliberately unused -- see class comment.
	const double Ordinal = OrdinalGetter->Read(PointIndex);
	const double T = FMath::Frac((Ordinal + 0.5) * PCGExSelectorInterleaved::GoldenRatioConjugate + EffectivePhase);

	const int32 k = PCGExCollections::Selectors::PickFromNormalized(T, Shared->Cumulative, Shared->TotalWeight, bUseWeights);
	return k == INDEX_NONE ? -1 : Target->Indices[k];
}

int32 FPCGExEntryInterleavedPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	check(OrdinalGetter);

	// Quota-only path: band resolves as usual, then walks to the nearest available entry.
	const double Ordinal = OrdinalGetter->Read(PointIndex);
	const double T = FMath::Frac((Ordinal + 0.5) * PCGExSelectorInterleaved::GoldenRatioConjugate + EffectivePhase);

	const int32 k = PCGExCollections::Selectors::PickFromNormalizedFiltered(T, Shared->Cumulative, Shared->TotalWeight, bUseWeights, InAvailability);
	return k == INDEX_NONE ? -1 : Target->Indices[k];
}

#pragma endregion

#pragma region UPCGExSelectorInterleavedFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorInterleavedFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryInterleavedPickerOp> NewOp = MakeShared<FPCGExEntryInterleavedPickerOp>();
	// LocalSeed rotates the phase so multiple Interleaved selectors (or the same one re-seeded)
	// de-correlate while staying deterministic. Golden-ratio hash keeps the rotation well-spread.
	NewOp->EffectivePhase = FMath::Frac(Config.Phase + BaseConfig.LocalSeed * PCGExSelectorInterleaved::GoldenRatioConjugate);
	NewOp->bUseWeights = Config.bUseWeights;
	NewOp->OrdinalSource = Config.OrdinalSource;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorInterleavedFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target || Target->Entries.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr<FPCGExInterleavedSharedData> NewShared = MakeShared<FPCGExInterleavedSharedData>();
	NewShared->TotalWeight = PCGExCollections::Selectors::BuildCumulativeWeights(Target, NewShared->Cumulative);
	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorInterleavedFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorInterleavedFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExSelectorInterleavedFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorInterleavedFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorInterleavedFactoryProviderSettings::GetDisplayName() const
{
	return Config.bUseWeights ? TEXT("Select : Interleaved Weighted") : TEXT("Select : Interleaved Uniform");
}
#endif

#pragma endregion
