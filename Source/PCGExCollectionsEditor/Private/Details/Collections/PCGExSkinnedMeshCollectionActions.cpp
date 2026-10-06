// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExSkinnedMeshCollectionActions.h"

#include "Collections/PCGExSkinnedMeshCollection.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExSkinnedMeshCollectionEditor.h"
#include "Engine/SkinnedAsset.h"

PCGEX_REGISTER_COLLECTION_EDITOR_TYPE(
	SkinnedMesh,
	UPCGExSkinnedMeshCollection,
	USkinnedAsset,
	"SMC_NewSkinnedMeshCollection",
	FLinearColor(FColor(0, 255, 255)),
	"Skinned Mesh Collection",
	"A weighted collection of skinned meshes with optional material overrides.",
	FPCGExSkinnedMeshCollectionEditor)

namespace PCGExSkinnedMeshCollectionActions
{
	// Tile-picker contribution (per-row resolution + typed editor default).
	struct FRegisterTilePicker
	{
		FRegisterTilePicker()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeRegistry::Get().Customize(PCGExAssetCollection::TypeIds::SkinnedMesh, [](FCollectionEditorTypeInfo& Info)
				{
					Info.TilePickerPropertyName = FName("SkinnedAsset");
					Info.TilePickerAllowedClass = USkinnedAsset::StaticClass();
				});
			});
		}
	} GRegisterSkinnedMeshTilePicker;
}
