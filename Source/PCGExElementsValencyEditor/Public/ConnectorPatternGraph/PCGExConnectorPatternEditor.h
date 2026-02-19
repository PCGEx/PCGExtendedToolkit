// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

struct FEdGraphEditAction;
class UPCGExConnectorPatternAsset;
class UPCGExConnectorPatternGraph;
class IDetailsView;
class SGraphEditor;

/**
 * Asset editor toolkit for Connector Pattern assets.
 * Two-tab layout: Graph (SGraphEditor) + Details (selected node properties).
 */
class PCGEXELEMENTSVALENCYEDITOR_API FPCGExConnectorPatternEditor : public FAssetEditorToolkit
{
public:
	FPCGExConnectorPatternEditor();
	virtual ~FPCGExConnectorPatternEditor() override;

	/** Initialize the editor for the given asset */
	void InitEditor(UPCGExConnectorPatternAsset* InAsset, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost);

	//~ FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override { return FName("PCGExConnectorPatternEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("Connector Pattern Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("ConnectorPattern"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.7f, 0.4f, 0.86f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void SaveAsset_Execute() override;

private:
	/** Create or load the graph for the asset */
	void EnsureGraphExists();

	/** Called when the graph is modified */
	void OnGraphChanged(const FEdGraphEditAction& Action);

	/** Called when graph node selection changes */
	void OnSelectionChanged(const TSet<UObject*>& NewSelection);

	/** Build the editor toolbar */
	void BuildEditorToolbar(FToolBarBuilder& ToolbarBuilder);

	/** Bind keyboard shortcuts for the graph editor */
	void BindGraphCommands();

	/** Spawn graph tab */
	TSharedRef<SDockTab> SpawnGraphTab(const FSpawnTabArgs& Args);

	/** Spawn details tab */
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);

	//~ Command handlers
	void DeleteSelectedNodes();
	bool CanDeleteNodes() const;
	void CopySelectedNodes();
	bool CanCopyNodes() const;
	void PasteNodes();
	bool CanPasteNodes() const;
	void CutSelectedNodes();
	void DuplicateSelectedNodes();
	void SelectAllNodes();
	void OnCreateComment();
	void ToggleSelectedPatternEnabled();
	bool CanTogglePatternEnabled() const;

	/** Tab IDs */
	static const FName GraphTabId;
	static const FName DetailsTabId;

	/** The asset being edited */
	TWeakObjectPtr<UPCGExConnectorPatternAsset> EditedAsset;

	/** The graph object */
	TObjectPtr<UPCGExConnectorPatternGraph> PatternGraph = nullptr;

	/** Graph editor widget */
	TSharedPtr<SGraphEditor> GraphEditorWidget;

	/** Details panel for selected nodes */
	TSharedPtr<IDetailsView> DetailsView;

	/** Command list for graph editor keyboard shortcuts */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	/** Delegate handle for graph changes */
	FDelegateHandle OnGraphChangedHandle;
};
