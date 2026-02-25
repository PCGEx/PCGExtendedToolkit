// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPCGExAssetCollection;

class FPCGExMeshCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	FPCGExMeshCollectionEditor();

	virtual FName GetToolkitFName() const override { return FName("PCGExMeshCollectionEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("PCGEx Mesh Collection Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("PCGEx"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }

protected:
	virtual void RegisterPropertyNameMapping(TMap<FName, FName>& Mapping) override;
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;
};
