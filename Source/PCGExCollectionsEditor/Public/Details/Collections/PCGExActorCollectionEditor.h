// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPCGExAssetCollection;

class FPCGExActorCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	FPCGExActorCollectionEditor();

	virtual FName GetToolkitFName() const override { return FName("PCGExActorCollectionEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("PCGEx Actor Collection Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("PCGEx"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }

protected:
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;
	virtual void BuildAddMenuContent(const TSharedRef<SVerticalBox>& MenuBox) override;
	virtual void CreateTabs(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs) override;
};
