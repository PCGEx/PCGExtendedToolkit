// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

#include "Widgets/Docking/SDockTab.h"

class UPCGExAssetCollection;
class SVerticalBox;
class FAssetThumbnailPool;
class SPCGExCollectionGridView;

namespace PCGExAssetCollectionEditor
{
	const FName EntriesName = FName("Entries");

	struct TabInfos
	{
		TabInfos() = default;

		TabInfos(const FName InId, const TSharedPtr<SWidget>& InView, const FName InLabel = NAME_None, const ETabRole InRole = PanelTab)
			: Id(InId), View(InView), Label(InLabel.IsNone() ? InId : InLabel), Role(InRole)
		{
		}

		FName Id = NAME_None;
		TSharedPtr<SWidget> Header = nullptr;
		TSharedPtr<SWidget> View = nullptr;
		TSharedPtr<SWidget> Footer = nullptr;
		TWeakPtr<SWidget> WeakView = nullptr;
		FName Label = NAME_None;
		ETabRole Role = PanelTab;
		FString Icon = TEXT("");
		bool bIsDetailsView = true;
	};

	struct FilterInfos
	{
		FilterInfos() = default;

		FilterInfos(const FName InId, const FText& InLabel, const FText& InToolTip)
			: Id(InId), Label(InLabel), ToolTip(InToolTip)
		{
		}

		FName Id = NAME_None;
		FText Label = FText::GetEmpty();
		FText ToolTip = FText::GetEmpty();
	};
}

class FPCGExAssetCollectionEditor : public FAssetEditorToolkit
{
public:
	FPCGExAssetCollectionEditor();
	virtual ~FPCGExAssetCollectionEditor() override;

	virtual void InitEditor(UPCGExAssetCollection* InCollection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost);
	virtual UPCGExAssetCollection* GetEditedCollection() const;

	virtual FName GetToolkitFName() const override { return FName("PCGExAssetCollectionEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("PCGEx Collection Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("PCGEx"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }

	TMap<FName, PCGExAssetCollectionEditor::FilterInfos> FilterInfos;

	/**
	 * Visibility check for properties under "Entries" array.
	 * Checks if property IS "Entries" or has "Entries" as an ancestor.
	 * Also allows properties from PropertyOverrides system (detects via struct inheritance from FPCGExPropertyCompiled).
	 *
	 * This supports full extensibility - custom property types just need to derive from FPCGExPropertyCompiled.
	 *
	 * Performance note: Parent chain depth is constant regardless of entry count.
	 * With 100s of entries, parent chain is still ~4-6 properties deep (e.g., Entries > Entry[0] > PropertyOverrides > Overrides > Value).
	 * Iterator is cheap - O(depth) where depth is constant, not O(entries).
	 */
	static bool IsPropertyUnderEntries(const FPropertyAndParent& PropertyAndParent);

protected:
	TWeakObjectPtr<UPCGExAssetCollection> EditedCollection;
	virtual void RegisterPropertyNameMapping(TMap<FName, FName>& Mapping);

	FReply FilterShowAll() const;
	FReply FilterHideAll() const;
	FReply ToggleFilter(const PCGExAssetCollectionEditor::FilterInfos Filter) const;

	virtual void CreateTabs(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs);
	void CreateEntriesTab(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs);
	void CreateGridTab(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs);
	virtual void BuildEditorToolbar(FToolBarBuilder& ToolbarBuilder);
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder);
	virtual void BuildAddMenuContent(const TSharedRef<SVerticalBox>& MenuBox);
	virtual void BuildAssetFooterToolbar(FToolBarBuilder& ToolbarBuilder);
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void ForceRefreshTabs();

	/** Returns the property name of the type-specific asset picker (e.g., "StaticMesh", "Actor").
	 *  Override in derived editors. Used by the grid view to build tile picker widgets. */
	virtual FName GetTilePickerPropertyName() const { return NAME_None; }

	/** Build the picker widget for a single tile entry. Override for custom picker logic. */
	virtual TSharedRef<SWidget> BuildTilePickerWidget(TSharedRef<IPropertyHandle> EntryHandle);

	TArray<PCGExAssetCollectionEditor::TabInfos> Tabs;
	FDelegateHandle OnHiddenAssetPropertyNamesChanged;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<SPCGExCollectionGridView> GridView;
};
