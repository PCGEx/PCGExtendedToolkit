// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

class SWidget;

#include "UObject/WeakObjectPtr.h"

#include "Toolkits/AssetEditorToolkit.h"

#include "Widgets/Docking/SDockTab.h"

#include "Details/Collections/PCGExCollectionCategoryGroups.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"

class FArrayProperty;
class UPCGExAssetCollection;
class UScriptStruct;
class SVerticalBox;
class FAssetThumbnailPool;
class SPCGExCollectionGridView;
struct FPropertyAndParent;

namespace PCGExAssetCollectionEditor
{
	struct PCGEXCOLLECTIONSEDITOR_API TabInfos
	{
		TabInfos() = default;

		TabInfos(const FName InId, const TSharedPtr<SWidget>& InView, const FName InLabel = NAME_None, const ETabRole InRole = PanelTab)
			: Id(InId)
			  , View(InView)
			  , Label(InLabel.IsNone() ? InId : InLabel)
			  , Role(InRole)
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

	/**
	 * A push option exposed in the grid view's right-pane combo button.
	 * When triggered, copies the listed top-level properties from the currently displayed
	 * (active) entry to every other entry in the multi-selection.
	 *
	 * Missing property names on the entry struct are silently skipped, which lets a single
	 * option safely target multiple collection types when shared (e.g. base options refer
	 * to FPCGExAssetCollectionEntry members; mesh-specific options refer to mesh-only ones).
	 */
	struct PCGEXCOLLECTIONSEDITOR_API FPushOption
	{
		FName Id = NAME_None;
		FText Label = FText::GetEmpty();
		FText Tooltip = FText::GetEmpty();
		TArray<FName> EntryPropertyNames;

		/**
		 * When true, the push respects per-element bEnabled gates: for an array of structs
		 * whose inner type has a bEnabled boolean (FPCGExPropertyOverrides::Overrides being
		 * the canonical case), elements whose target has bEnabled == true are skipped.
		 * When false, properties are unconditionally overwritten with the source value.
		 */
		bool bRespectEnabledGate = false;
	};
}

/**
 * Base editor toolkit for PCGEx asset collections (Mesh, Actor, Level, PCGDataAsset, etc.).
 *
 * To create a custom collection editor:
 * 1. Subclass this editor and override the tile picker virtuals:
 *    - GetTilePickerPropertyName() -- return the FName of the asset property on your entry struct (e.g., "StaticMesh")
 *    - GetTilePickerAllowedClass() -- return the UClass* for the asset picker filter
 *    - BuildTilePickerWidget() -- (optional) fully custom picker widget per tile
 * 2. Override CreateTabs() / BuildEditorToolbar() / BuildAssetHeaderToolbar() for custom tabs and toolbar buttons.
 * 3. Register via FAssetTypeActions_Base::OpenAssetEditor -- create a TSharedRef<YourEditor>, call InitEditor().
 *
 * Footer filter groups need no editor code: tag entry UPROPERTYs with meta=(PCGExCategoryGroup="<id>")
 * and register the id's display info with PCGExCollectionCategoryGroups::FRegistry.
 *
 * See FPCGExMeshCollectionEditor, FPCGExActorCollectionEditor, etc. for reference implementations.
 */
struct FPropertyChangedEvent;

class PCGEXCOLLECTIONSEDITOR_API FPCGExAssetCollectionEditor : public FAssetEditorToolkit
{
public:
	FPCGExAssetCollectionEditor();
	virtual ~FPCGExAssetCollectionEditor() override;

	virtual void InitEditor(UPCGExAssetCollection* InCollection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost);
	virtual UPCGExAssetCollection* GetEditedCollection() const;

	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExAssetCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Collection Editor");
	}

	virtual FString GetWorldCentricTabPrefix() const override
	{
		return TEXT("PCGEx");
	}

	virtual FLinearColor GetWorldCentricTabColorScale() const override
	{
		return FLinearColor::White;
	}

	/**
	 * Entries-tab scope: the Entries array and everything beneath it, plus the overrides rows -- owned by
	 * FPCGExPropertyOverrides, FPCGExPropertyOverrideEntry or an FPCGExProperty type, which covers the external
	 * structures PCGExPropertiesEditor hosts (their parent chain never reaches Entries). The CategoryOverrides
	 * subtree is rejected first: same overrides types, but collection-level.
	 */
	static bool IsPropertyUnderEntries(const FPropertyAndParent& PropertyAndParent, const FArrayProperty* EntriesProperty);

protected:
	TWeakObjectPtr<UPCGExAssetCollection> EditedCollection;

	/**
	 * Register push options exposed by the grid view side panel.
	 * Override in derived editors to append type-specific bundles (e.g. mesh adds Material
	 * Variants and Descriptors). Always call Super first to keep shared options.
	 */
	virtual void RegisterPushOptions(TArray<PCGExAssetCollectionEditor::FPushOption>& OutOptions);

	/**
	 * Entry structs the footer filter groups are discovered from. Base: the collection type's registered
	 * entry struct, or every registered entry struct when the type has none (Omni). Override for hosts whose
	 * payloads come from elsewhere (Variant).
	 */
	virtual void GetFilterableEntryStructs(TArray<const UScriptStruct*>& OutStructs) const;

	/** Footer "Filters" strip over the discovered groups (discovery runs once per editor). */
	TSharedRef<SWidget> MakeFilterBar();

	/** Rebuild every non-tab details panel that installs the category-group filter (grid panels). */
	virtual void RefreshFilteredPanels();

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

	/** Rebuild every picker that snapshots host settings at build time (tile pickers, entry customizations). */
	void RefreshPickerWidgets();
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	/**
	 * Layout/app identity. Subclasses whose tab set differs from the base MUST override all
	 * three: tab-manager state persists per layout name in GEditorLayoutIni, and a shared
	 * name restores another editor's tab layout — unregistered tab ids then warn with
	 * "Cannot spawn tab because no spawner is registered" and render as unrecognized tabs.
	 */
	virtual FName GetLayoutName() const
	{
		return FName("PCGExAssetCollectionEditor_Layout_v9");
	}

	virtual FName GetAppIdentifier() const
	{
		return FName("PCGExAssetCollectionEditor");
	}

	/** Tab forcibly foregrounded on open (see InitEditor). Must be one of the Tabs ids. */
	virtual FName GetDefaultForegroundTabId() const
	{
		return FName("Grid");
	}

	/** Property name of the type-specific asset picker (e.g. "StaticMesh"). Base resolves
	 *  the edited collection's registered TilePickerPropertyName; override as needed. */
	virtual FName GetTilePickerPropertyName() const;

	/** Per-row picker resolution: the row's entry type decides; falls back to the
	 *  editor-wide virtuals when the type has no registered picker info. */
	void ResolveTilePickerForRow(int32 EntryIndex, FName& OutPropertyName, const UClass*& OutAllowedClass, TFunction<bool(const FAssetData&)>* OutShouldFilterAsset = nullptr) const;

	/** Build the picker widget for a single tile entry. Override for custom picker logic.
	 *  Invoke OnPropertyEdited with the FName of the entry-struct property that was written. */
	virtual TSharedRef<SWidget> BuildTilePickerWidget(
		TWeakObjectPtr<UPCGExAssetCollection> Collection,
		int32 EntryIndex,
		FOnTilePropertyEdited OnPropertyEdited);

	/** Standard SubCollection picker slot (visible only when bIsSubCollection is true).
	 *  Writes the base FPCGExAssetCollectionEntry::SubCollection property; any collection
	 *  type is accepted. */
	TSharedRef<SWidget> BuildSubCollectionPickerSlot(
		TWeakObjectPtr<UPCGExAssetCollection> Collection,
		int32 EntryIndex,
		FOnTilePropertyEdited OnPropertyEdited) const;

	/** Allowed UClass for the type-specific asset picker. Base resolves the registered
	 *  TilePickerAllowedClass; override in subclasses. */
	virtual const UClass* GetTilePickerAllowedClass() const;

	TArray<PCGExAssetCollectionEditor::TabInfos> Tabs;
	TArray<PCGExCollectionCategoryGroups::FGroup> FilterGroups;
	bool bFilterGroupsDiscovered = false;
	FDelegateHandle OnHiddenCategoryGroupsChangedHandle;
	FDelegateHandle OnObjectPropertyChangedHandle;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<SPCGExCollectionGridView> GridView;
};
