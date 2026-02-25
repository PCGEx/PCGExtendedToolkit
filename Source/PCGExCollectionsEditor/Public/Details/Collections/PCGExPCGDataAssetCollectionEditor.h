// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPCGExAssetCollection;

class FPCGExPCGDataAssetCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	FPCGExPCGDataAssetCollectionEditor();

	virtual FName GetToolkitFName() const override { return FName("PCGExPCGDataAssetCollectionEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("PCGEx PCGDataAsset Collection Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("PCGEx"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }

protected:
	virtual FName GetTilePickerPropertyName() const override { return FName("DataAsset"); }
	virtual void GetAdditionalTilePropertyNames(TSet<FName>& OutNames) const override { OutNames.Add(FName("Source")); OutNames.Add(FName("Level")); }
	virtual TSharedRef<SWidget> BuildTilePickerWidget(TSharedRef<IPropertyHandle> EntryHandle) override;
};
