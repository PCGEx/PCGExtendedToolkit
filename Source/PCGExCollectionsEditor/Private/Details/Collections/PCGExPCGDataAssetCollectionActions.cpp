// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExPCGDataAssetCollectionActions.h"

#include "PCGDataAsset.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExPCGDataAssetCollectionEditor.h"

PCGEX_REGISTER_COLLECTION_EDITOR_TYPE(
	PCGDataAsset,
	UPCGExPCGDataAssetCollection,
	UPCGDataAsset,
	"SMC_NewPCGDataAssetCollection",
	FLinearColor(FColor(100, 150, 200)),
	"PCGDataAsset Collection",
	"A weighted collection of PCG Data Assets.",
	FPCGExPCGDataAssetCollectionEditor)

// PCGDataAsset collections aren't created from the content-browser right-click flow.
namespace
{
	struct FCustomizePCGDataAssetEditorTypeInfo
	{
		FCustomizePCGDataAssetEditorTypeInfo()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeRegistry::Get().Customize(
					PCGExAssetCollection::TypeIds::PCGDataAsset,
					[](FCollectionEditorTypeInfo& Info)
					{
						Info.bSupportsMenuCreation = false;

						// Tile-picker contribution. Covers the DataAsset source mode; Level-mode
						// rows are edited through the entry details.
						Info.TilePickerPropertyName = FName("DataAsset");
						Info.TilePickerAllowedClass = UPCGDataAsset::StaticClass();
					});
			});
		}
	} GCustomizePCGDataAssetEditorTypeInfo;
}

namespace PCGExPCGDataAssetCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets)
	{
		PCGExCollectionEditorHelpers::CreateCollectionFromTyped(SelectedAssets, UPCGExPCGDataAssetCollection::StaticClass(), TEXT("SMC_NewPCGDataAssetCollection"));
	}

	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExPCGDataAssetCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets)
	{
		TArray<TObjectPtr<UPCGExAssetCollection>> AsBase;
		AsBase.Reserve(SelectedCollections.Num());
		for (const TObjectPtr<UPCGExPCGDataAssetCollection>& C : SelectedCollections)
		{
			AsBase.Add(C);
		}
		PCGExCollectionEditorHelpers::UpdateCollectionsFromTyped(AsBase, SelectedAssets);
	}
}
