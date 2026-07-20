// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExActorCollectionActions.h"

#include "PCGExCollectionsEditorMenuUtils.h"
#include "Collections/PCGExActorCollection.h"
#include "Details/Collections/PCGExActorCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "GameFramework/Actor.h"

PCGEX_REGISTER_COLLECTION_EDITOR_TYPE(
	Actor,
	UPCGExActorCollection,
	AActor,
	"SMC_NewActorCollection",
	FLinearColor(FColor(67, 142, 245)),
	"Actor Collection",
	"A weighted collection of actor classes for spawning.",
	FPCGExActorCollectionEditor)

// Actor BPs surface as UBlueprint, not AActor -- override the default IsInstanceOf detection
// to walk the ParentClass tag instead.
namespace
{
	struct FCustomizeActorEditorTypeInfo
	{
		FCustomizeActorEditorTypeInfo()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeRegistry::Get().Customize(
					PCGExAssetCollection::TypeIds::Actor,
					[](FCollectionEditorTypeInfo& Info)
					{
						Info.DetectSourceAsset = [](const FAssetData& Asset)
						{
							return PCGExCollectionsEditorMenuUtils::DoesAssetInheritFromAActor(Asset);
						};

						// Tile-picker contribution (per-row resolution + typed editor default).
						Info.TilePickerPropertyName = FName("Actor");
						Info.TilePickerAllowedClass = AActor::StaticClass();
					});
			});
		}
	} GCustomizeActorEditorTypeInfo;
}

namespace PCGExActorCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets)
	{
		PCGExCollectionEditorHelpers::CreateCollectionFromTyped(SelectedAssets, UPCGExActorCollection::StaticClass(), TEXT("SMC_NewActorCollection"));
	}

	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExActorCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets)
	{
		TArray<TObjectPtr<UPCGExAssetCollection>> AsBase;
		AsBase.Reserve(SelectedCollections.Num());
		for (const TObjectPtr<UPCGExActorCollection>& C : SelectedCollections)
		{
			AsBase.Add(C);
		}
		PCGExCollectionEditorHelpers::UpdateCollectionsFromTyped(AsBase, SelectedAssets);
	}
}
