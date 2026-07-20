// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExOmniCollectionActions.h"

#include "Collections/PCGExOmniCollection.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExOmniCollectionEditor.h"

// Registered by hand (no single source asset class): "Create from selection" claims any
// asset another type's detector claims. Opens in FPCGExOmniCollectionEditor -- a thin
// subclass of the (already heterogeneous) base editor adding Omni-only toolbar affordances.
namespace PCGExOmniCollectionActions
{
	struct FRegisterOmniEditorTypeInfo
	{
		FRegisterOmniEditorTypeInfo()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeInfo Info;
				Info.Id = PCGExAssetCollection::TypeIds::Omni;
				Info.CollectionClass = UPCGExOmniCollection::StaticClass();
				Info.SourceAssetClass = nullptr;
				Info.DefaultAssetNamePrefix = TEXT("SMC_NewOmniCollection");
				Info.AssetColor = FLinearColor(FColor(255, 255, 255));
				Info.DisplayName = INVTEXT("Omni Collection");
				Info.AssetDescription = INVTEXT("A weighted collection of mixed entry types -- meshes, actors, levels, data assets and custom types in a single list.");
				Info.DetectSourceAsset = [](const FAssetData& Asset)
				{
					// Anything another registered type claims can seed an Omni entry.
					bool bClaimed = false;
					FCollectionEditorTypeRegistry::Get().ForEach([&bClaimed, &Asset](const FCollectionEditorTypeInfo& Other)
					{
						if (bClaimed || Other.Id == PCGExAssetCollection::TypeIds::Omni || !Other.DetectSourceAsset)
						{
							return;
						}
						bClaimed = Other.DetectSourceAsset(Asset);
					});
					return bClaimed;
				};
				Info.DetectCollectionAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<UPCGExOmniCollection>(); };
				Info.OpenEditor = [](UPCGExAssetCollection* Collection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host)
				{
					const TSharedRef<FPCGExOmniCollectionEditor> Editor = MakeShared<FPCGExOmniCollectionEditor>();
					Editor->InitEditor(Collection, Mode, Host);
				};
				Info.CreateCollection = [](const TArray<FAssetData>& Assets)
				{
					PCGExCollectionEditorHelpers::CreateCollectionFromTyped(Assets, UPCGExOmniCollection::StaticClass(), TEXT("SMC_NewOmniCollection"));
				};
				Info.UpdateCollections = &PCGExCollectionEditorHelpers::UpdateCollectionsFromTyped;
				FCollectionEditorTypeRegistry::Get().Register(MoveTemp(Info));
			});
		}
	} GRegisterOmniEditorTypeInfo;
}
