// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExVariantCollectionActions.h"

#include "Collections/PCGExVariantCollection.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExVariantCollectionEditor.h"

// Variant collections are registered by hand rather than via PCGEX_REGISTER_COLLECTION_EDITOR_TYPE:
// the macro wires CreateCollection/DetectSourceAsset for the content-browser "Create from
// selection" flow, which variants (asset-only) opt out of entirely.
namespace PCGExVariantCollectionActions
{
	struct FRegisterVariantEditorTypeInfo
	{
		FRegisterVariantEditorTypeInfo()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeInfo Info;
				Info.Id = PCGExAssetCollection::TypeIds::Variant;
				Info.CollectionClass = UPCGExVariantCollection::StaticClass();
				Info.SourceAssetClass = nullptr;
				Info.DefaultAssetNamePrefix = TEXT("SMC_NewVariantCollection");
				Info.AssetColor = FLinearColor(FColor(200, 120, 220));
				Info.DisplayName = INVTEXT("Variant Collection");
				Info.AssetDescription = INVTEXT("Per-entry overrides for one or more source collections, for end-of-pipeline asset swapping (biomes, themes).");
				Info.bSupportsMenuCreation = false;
				Info.DetectSourceAsset = [](const FAssetData&) { return false; };
				Info.DetectCollectionAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<UPCGExVariantCollection>(); };
				Info.OpenEditor = [](UPCGExAssetCollection* Collection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host)
				{
					const TSharedRef<FPCGExVariantCollectionEditor> Editor = MakeShared<FPCGExVariantCollectionEditor>();
					Editor->InitEditor(Collection, Mode, Host);
				};
				Info.CreateCollection = [](const TArray<FAssetData>&) {};
				Info.UpdateCollections = [](const TArray<TObjectPtr<UPCGExAssetCollection>>&, const TArray<FAssetData>&) {};
				FCollectionEditorTypeRegistry::Get().Register(MoveTemp(Info));
			});
		}
	} GRegisterVariantEditorTypeInfo;
}
