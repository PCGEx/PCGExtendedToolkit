// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class SPCGExVariantGridView;
struct FAssetData;

/**
 * Variant collection editor. Replaces the standard entry grid with the variant grid:
 * one group per source collection, one tile per SOURCE entry (auto-populated), opt-in
 * swap declaration per tile. EntryId binding is fully abstracted away.
 */
class FPCGExVariantCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	FPCGExVariantCollectionEditor();

	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExVariantCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Variant Collection Editor");
	}

	virtual FString GetWorldCentricTabPrefix() const override
	{
		return TEXT("PCGEx");
	}

	virtual FLinearColor GetWorldCentricTabColorScale() const override
	{
		return FLinearColor::White;
	}

protected:
	// Distinct layout/app identity — this editor's tab set differs from the base, and layout
	// persistence is keyed by these names (see FPCGExAssetCollectionEditor::GetLayoutName).
	virtual FName GetLayoutName() const override
	{
		return FName("PCGExVariantCollectionEditor_Layout_v1");
	}

	virtual FName GetAppIdentifier() const override
	{
		return FName("PCGExVariantCollectionEditor");
	}

	virtual FName GetDefaultForegroundTabId() const override
	{
		return FName("Swaps");
	}

	virtual void CreateTabs(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs) override;
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;

private:
	TSharedPtr<SPCGExVariantGridView> VariantGrid;

	TSharedRef<SWidget> MakeAddSourceMenu();
	void OnSourceAssetPicked(const FAssetData& AssetData);

	TSharedRef<SWidget> MakeAddAssetSwapMenu();
	void OnSwapAssetPicked(const FAssetData& AssetData);

	FReply OnSyncMappings();
};
