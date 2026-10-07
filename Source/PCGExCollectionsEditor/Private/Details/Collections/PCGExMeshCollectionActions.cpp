// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExMeshCollectionActions.h"

#include "Collections/PCGExMeshCollection.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Details/Collections/PCGExMeshCollectionEditor.h"
#include "Engine/StaticMesh.h"

PCGEX_REGISTER_COLLECTION_EDITOR_TYPE(
	Mesh,
	UPCGExMeshCollection,
	UStaticMesh,
	"SMC_NewMeshCollection",
	FLinearColor(FColor(0, 255, 255)),
	"Mesh Collection",
	"A weighted collection of static meshes with optional material overrides.",
	FPCGExMeshCollectionEditor)

namespace PCGExMeshCollectionActions
{
	// Tile-picker contribution (per-row resolution + typed editor default). Same TU as the
	// macro registration above -- sequential static init keeps the order safe.
	struct FRegisterTilePicker
	{
		FRegisterTilePicker()
		{
			FCollectionEditorTypeRegistry::AddPendingRegistration([]()
			{
				FCollectionEditorTypeRegistry::Get().Customize(PCGExAssetCollection::TypeIds::Mesh, [](FCollectionEditorTypeInfo& Info)
				{
					Info.TilePickerPropertyName = FName("StaticMesh");
					Info.TilePickerAllowedClass = UStaticMesh::StaticClass();
				});
			});
		}
	} GRegisterMeshTilePicker;
}
