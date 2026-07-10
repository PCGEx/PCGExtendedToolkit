// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorNoiseField.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExNoiseGenerator.h"

#pragma region FPCGExEntryNoiseFieldPickerOp

void FPCGExEntryNoiseFieldPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Noise Field -- failed to build shared weight tables. Check that the target category has at least one valid entry."))
}

bool FPCGExEntryNoiseFieldPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	if (!Noise || !Noise->IsValid())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Noise Field -- no valid Noise3D input."))
		return false;
	}

	const UPCGBasePointData* PointData = InDataFacade->Source->GetIn();
	if (!PointData)
	{
		return false;
	}

	TransformRange = PointData->GetConstTransformValueRange();
	return true;
}

int32 FPCGExEntryNoiseFieldPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	// Per-point Seed is deliberately unused -- see class comment. PickFromNormalized clamps,
	// so post-scale noise values outside [0, 1] saturate into the edge bands.
	const double Value = Noise->GetDouble(TransformRange[PointIndex].GetLocation());

	const int32 k = PCGExCollections::Selectors::PickFromNormalized(Value, Shared->Cumulative, Shared->TotalWeight, bUseWeights);
	return k == INDEX_NONE ? -1 : Target->Indices[k];
}

int32 FPCGExEntryNoiseFieldPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	// Quota-only path: band resolves as usual, then walks to the nearest available entry --
	// preserves spatial coherence as closely as capacity allows.
	const double Value = Noise->GetDouble(TransformRange[PointIndex].GetLocation());

	const int32 k = PCGExCollections::Selectors::PickFromNormalizedFiltered(Value, Shared->Cumulative, Shared->TotalWeight, bUseWeights, InAvailability);
	return k == INDEX_NONE ? -1 : Target->Indices[k];
}

#pragma endregion

#pragma region UPCGExSelectorNoiseFieldFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorNoiseFieldFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	if (!Noise || !Noise->IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FPCGExEntryNoiseFieldPickerOp> NewOp = MakeShared<FPCGExEntryNoiseFieldPickerOp>();
	NewOp->Noise = Noise;
	NewOp->bUseWeights = Config.bUseWeights;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorNoiseFieldFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target || Target->Entries.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr<FPCGExNoiseFieldSharedData> NewShared = MakeShared<FPCGExNoiseFieldSharedData>();
	NewShared->TotalWeight = PCGExCollections::Selectors::BuildCumulativeWeights(Target, NewShared->Cumulative);
	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorNoiseFieldFactoryProviderSettings

TArray<FPCGPinProperties> UPCGExSelectorNoiseFieldFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExCollections::Labels::SourceNoiseLabel, "Noise3D definitions sampled at each point's position. Multiple inputs blend per their own blend modes/weights.", Required, FPCGExDataTypeInfoNoise3D::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExSelectorNoiseFieldFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> Noise = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!Noise->Init(InContext, PCGExCollections::Labels::SourceNoiseLabel, true))
	{
		return nullptr;
	}

	UPCGExSelectorNoiseFieldFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorNoiseFieldFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	NewFactory->Noise = Noise;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorNoiseFieldFactoryProviderSettings::GetDisplayName() const
{
	return TEXT("Select : Noise Field");
}
#endif

#pragma endregion
