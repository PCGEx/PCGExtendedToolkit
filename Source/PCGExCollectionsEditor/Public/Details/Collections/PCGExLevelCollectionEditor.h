// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPCGExAssetCollection;

class FPCGExLevelCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	FPCGExLevelCollectionEditor();

	virtual FName GetToolkitFName() const override { return FName("PCGExLevelCollectionEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("PCGEx Level Collection Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("PCGEx"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }
};
