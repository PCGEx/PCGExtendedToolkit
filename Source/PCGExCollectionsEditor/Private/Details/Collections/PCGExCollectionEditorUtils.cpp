// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionEditorUtils.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "Core/PCGExAssetCollection.h"

namespace PCGExCollectionEditorUtils
{
#define PCGEX_IF_TYPE(_NAME, _BODY) { if (UPCGEx##_NAME##Collection* Collection = Cast<UPCGEx##_NAME##Collection>(InCollection)) { _BODY; return; }}
#define PCGEX_PER_COLLECTION(_BODY)	PCGEX_FOREACH_COLLECTION_TYPE(PCGEX_IF_TYPE, _BODY)

	// Notify listeners that the collection was modified (for grid view refresh, etc.)
	static void NotifyModified(UPCGExAssetCollection* InCollection)
	{
		(void)InCollection->MarkPackageDirty();
		FCoreUObjectDelegates::BroadcastOnObjectModified(InCollection);
	}

	void AddBrowserSelection(UPCGExAssetCollection* InCollection)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

		if (SelectedAssets.IsEmpty()) { return; }

		InCollection->EDITOR_AddBrowserSelectionTyped(SelectedAssets);
	}

	void SortByWeightAscending(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Sort Collection by Weight (Ascending)"));
		InCollection->Modify();
		InCollection->Sort([&](const FPCGExAssetCollectionEntry* A, const FPCGExAssetCollectionEntry* B) { return A->Weight < B->Weight; });
		NotifyModified(InCollection);
	}

	void SortByWeightDescending(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Sort Collection by Weight (Descending)"));
		InCollection->Modify();
		InCollection->Sort([&](const FPCGExAssetCollectionEntry* A, const FPCGExAssetCollectionEntry* B) { return A->Weight > B->Weight; });
		NotifyModified(InCollection);
	}

	void SetWeightIndex(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Set Weights to Index"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i) { Entry->Weight = i + 1; });
		NotifyModified(InCollection);
	}

	void PadWeight(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Pad Weights (+1)"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i) { Entry->Weight += 1; });
		NotifyModified(InCollection);
	}

	void MultWeight(UPCGExAssetCollection* InCollection, int32 Mult)
	{
		FScopedTransaction Transaction(FText::Format(INVTEXT("Multiply Weights (x{0})"), FText::AsNumber(Mult)));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i) { Entry->Weight *= Mult; });
		NotifyModified(InCollection);
	}

	void WeightOne(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Reset Weights to 100"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i) { Entry->Weight = 100; });
		NotifyModified(InCollection);
	}

	void WeightRandom(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Randomize Weights"));
		InCollection->Modify();
		FRandomStream RandomSource(FMath::Rand());
		const int32 NumEntries = InCollection->NumEntries();
		InCollection->ForEachEntry(
			[&](FPCGExAssetCollectionEntry* Entry, int32 i)
			{
				Entry->Weight = RandomSource.RandRange(1, NumEntries * 100);
			});
		NotifyModified(InCollection);
	}

	void NormalizedWeightToSum(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Normalize Weights to 100"));
		InCollection->Modify();
		double Sum = 0;

		InCollection->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 i) { Sum += Entry->Weight; });
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			int32& W = Entry->Weight;

			if (W <= 0)
			{
				W = 0;
				return;
			}

			const double Weight = (static_cast<double>(W) / Sum) * 100;
			W = Weight;
		});
		NotifyModified(InCollection);
	}
}
