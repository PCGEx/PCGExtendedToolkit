// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExGenericCollectionEntry.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"

// Entry-only type: no collection class, no asset definition, no content-browser flows (every closure
// stays null, which is how the menu utils and Omni's create-from-selection detector skip it). The
// registration exists for the per-row tile picker on heterogeneous grids.
namespace PCGExGenericEntryActions
{
	struct FRegisterGenericEditorTypeInfo
	{
		FRegisterGenericEditorTypeInfo()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeInfo Info;
				Info.Id = PCGExAssetCollection::TypeIds::Generic;
				Info.CollectionClass = nullptr;
				Info.SourceAssetClass = nullptr;
				Info.DisplayName = INVTEXT("Generic Entry");
				Info.AssetDescription = INVTEXT("An entry referencing any asset, with no spawn behaviour of its own.");
				Info.bSupportsMenuCreation = false;

				// UObject is SObjectPropertyEntryBox's own default: the unfiltered asset picker.
				Info.TilePickerPropertyName = FName("Asset");
				Info.TilePickerAllowedClass = UObject::StaticClass();
				Info.ResolveTilePickerAllowedClass = [](const UPCGExAssetCollection* Host) -> const UClass*
				{
					return (Host && Host->GenericAllowedClass.Get()) ? Host->GenericAllowedClass.Get() : UObject::StaticClass();
				};

				FCollectionEditorTypeRegistry::Get().Register(MoveTemp(Info));
			});
		}
	} GRegisterGenericEditorTypeInfo;
}
