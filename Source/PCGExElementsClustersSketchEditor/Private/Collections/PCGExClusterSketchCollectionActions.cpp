// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExClusterSketchCollectionActions.h"

#include "Collections/PCGExClusterSketchCollection.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Sketch/PCGExClusterSketch.h"

namespace PCGExClusterSketchCollectionActions
{
	// Hand-built FCollectionEditorTypeInfo, called from StartupModule -- see the header. The
	// PCGEX_REGISTER_COLLECTION_EDITOR_TYPE* macros all emit static auto-registrars, which are only
	// safe for types living inside PCGExCollectionsEditor itself.
	void RegisterEditorType()
	{
		FCollectionEditorTypeInfo Info;
		Info.Id = PCGExSketch::CollectionTypeId;
		Info.CollectionClass = UPCGExClusterSketchCollection::StaticClass();
		Info.SourceAssetClass = UPCGExClusterSketch::StaticClass();
		Info.DefaultAssetNamePrefix = TEXT("SMC_NewClusterSketchCollection");
		Info.AssetColor = FLinearColor(FColor(224, 128, 255));
		Info.DisplayName = INVTEXT("Cluster Sketch Collection");
		Info.AssetDescription = INVTEXT("A weighted collection of Cluster Sketch assets.");
		Info.DetectSourceAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<UPCGExClusterSketch>(); };
		Info.DetectCollectionAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<UPCGExClusterSketchCollection>(); };

		// Base toolkit: a sketch entry has no type-specific authoring surface, so there is nothing
		// for a bespoke editor to add.
		Info.OpenEditor = [](UPCGExAssetCollection* Collection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host)
		{
			TSharedRef<FPCGExAssetCollectionEditor> Editor = MakeShared<FPCGExAssetCollectionEditor>();
			Editor->InitEditor(Collection, Mode, Host);
		};
		Info.CreateCollection = [](const TArray<FAssetData>& Assets)
		{
			PCGExCollectionEditorHelpers::CreateCollectionFromTyped(Assets, UPCGExClusterSketchCollection::StaticClass(), TEXT("SMC_NewClusterSketchCollection"));
		};
		Info.UpdateCollections = &PCGExCollectionEditorHelpers::UpdateCollectionsFromTyped;

		// Per-row tile picker, so a sketch row in a mixed Omni grid gets a sketch picker.
		Info.TilePickerPropertyName = FName("Sketch");
		Info.TilePickerAllowedClass = UPCGExClusterSketch::StaticClass();

		FCollectionEditorTypeRegistry::Get().Register(MoveTemp(Info));
	}
}
