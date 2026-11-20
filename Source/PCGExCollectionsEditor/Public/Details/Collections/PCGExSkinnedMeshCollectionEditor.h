// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPCGExAssetCollection;

/** Skinned mesh collection editor -- mirrors FPCGExMeshCollectionEditor. */
class FPCGExSkinnedMeshCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExSkinnedMeshCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Skinned Mesh Collection Editor");
	}

protected:
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;

	virtual FName GetTilePickerPropertyName() const override
	{
		return FName("SkinnedAsset");
	}

	virtual const UClass* GetTilePickerAllowedClass() const override;
};
