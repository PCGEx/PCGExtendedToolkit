// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

/** Level collection editor -- minimal reference implementation. */
class FPCGExLevelCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExLevelCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Level Collection Editor");
	}

protected:
	virtual FName GetTilePickerPropertyName() const override
	{
		return FName("Level");
	}

	virtual const UClass* GetTilePickerAllowedClass() const override;
};
