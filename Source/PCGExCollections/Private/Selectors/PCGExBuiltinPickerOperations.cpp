// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExBuiltinPickerOperations.h"

#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMath.h"
#include "Selectors/PCGExSelectorHelpers.h"

#pragma region FPCGExEntryWeightedRandomPickerOp

int32 FPCGExEntryWeightedRandomPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	return Target->GetPickRandomWeighted(Seed);
}

int32 FPCGExEntryWeightedRandomPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	return PCGExCollections::Selectors::FilteredWeightedPick(Target, InAvailability, Seed);
}

#pragma endregion

#pragma region FPCGExEntryRandomPickerOp

int32 FPCGExEntryRandomPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());
	return Target->GetPickRandom(Seed);
}

int32 FPCGExEntryRandomPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const int32 N = Target->Entries.Num();
	int32 NumAvailable = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (InAvailability.IsAvailable(i))
		{
			++NumAvailable;
		}
	}
	if (NumAvailable == 0)
	{
		return -1;
	}

	int32 Roll = FRandomStream(Seed).RandRange(0, NumAvailable - 1);
	for (int32 i = 0; i < N; ++i)
	{
		if (InAvailability.IsAvailable(i) && Roll-- == 0)
		{
			return Target->Indices[i];
		}
	}
	return -1;
}

#pragma endregion

#pragma region FPCGExEntryIndexPickerOp

bool FPCGExEntryIndexPickerOp::PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection)
{
	if (!FPCGExEntryPickerOperation::PrepareForData(InContext, InDataFacade, InTarget, InOwningCollection))
	{
		return false;
	}

	const bool bWantsMinMax = IndexConfig.bRemapIndexToCollectionSize;
	IndexGetter = IndexConfig.GetValueSettingIndex();
	if (!IndexGetter->Init(InDataFacade, !bWantsMinMax, bWantsMinMax))
	{
		return false;
	}

	MaxInputIndex = IndexGetter->Max();
	return true;
}

int32 FPCGExEntryIndexPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const int32 MaxIndex = Target->Num() - 1;
	double UserIndex = IndexGetter->Read(PointIndex);

	if (IndexConfig.bRemapIndexToCollectionSize && MaxInputIndex > 0)
	{
		// Input min is deliberately 0, not the observed min: values are treated as 0-based
		// indices and remap only compresses the top end. Changing this would silently alter
		// picks in existing graphs whose index attribute doesn't start at 0.
		UserIndex = PCGExMath::Remap(UserIndex, 0, MaxInputIndex, 0, MaxIndex);
		UserIndex = PCGExMath::TruncateDbl(UserIndex, IndexConfig.TruncateRemap);
	}

	const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(UserIndex), MaxIndex, IndexConfig.IndexSafety);
	return Target->GetPick(Sanitized, IndexConfig.PickMode);
}

#pragma endregion

#pragma region FPCGExMicroWeightedRandomPickerOp

int32 FPCGExMicroWeightedRandomPickerOp::Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const
{
	if (!InMicroCache || InMicroCache->IsEmpty())
	{
		return -1;
	}
	return InMicroCache->GetPickRandomWeighted(Seed);
}

#pragma endregion

#pragma region FPCGExMicroRandomPickerOp

int32 FPCGExMicroRandomPickerOp::Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const
{
	if (!InMicroCache || InMicroCache->IsEmpty())
	{
		return -1;
	}
	return InMicroCache->GetPickRandom(Seed);
}

#pragma endregion

#pragma region FPCGExMicroIndexPickerOp

bool FPCGExMicroIndexPickerOp::PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	if (!FPCGExMicroEntryPickerOperation::PrepareForData(InContext, InDataFacade))
	{
		return false;
	}

	// Mirror the entry picker: min/max capture is only needed by the remap path, and scoped
	// reads must be disabled when capturing (full scan required).
	const bool bWantsMinMax = IndexConfig.bRemapIndexToCollectionSize;
	IndexGetter = IndexConfig.GetValueSettingIndex();
	if (!IndexGetter->Init(InDataFacade, !bWantsMinMax, bWantsMinMax))
	{
		return false;
	}

	MaxInputIndex = IndexGetter->Max();
	return true;
}

int32 FPCGExMicroIndexPickerOp::Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const
{
	if (!InMicroCache || InMicroCache->IsEmpty())
	{
		return -1;
	}

	check(IndexGetter);

	const int32 MaxIndex = InMicroCache->Num() - 1;
	double UserIndex = IndexGetter->Read(PointIndex);

	if (IndexConfig.bRemapIndexToCollectionSize && MaxInputIndex > 0)
	{
		// Same contract as the entry picker: 0-based input remapped to the micro cache size.
		UserIndex = PCGExMath::Remap(UserIndex, 0, MaxInputIndex, 0, MaxIndex);
		UserIndex = PCGExMath::TruncateDbl(UserIndex, IndexConfig.TruncateRemap);
	}

	const int32 Sanitized = PCGExMath::SanitizeIndex(static_cast<int32>(UserIndex), MaxIndex, IndexConfig.IndexSafety);
	return InMicroCache->GetPick(Sanitized, IndexConfig.PickMode);
}

#pragma endregion
