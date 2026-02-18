// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternEditor.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphSchema.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "Core/PCGExValencyConnectorSet.h"

#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_Comment.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"

const FName FPCGExConnectorPatternEditor::GraphTabId = FName("ConnectorPatternGraphTab");
const FName FPCGExConnectorPatternEditor::DetailsTabId = FName("ConnectorPatternDetailsTab");

#pragma region FPCGExConnectorPatternEditor

FPCGExConnectorPatternEditor::FPCGExConnectorPatternEditor()
{
}

FPCGExConnectorPatternEditor::~FPCGExConnectorPatternEditor()
{
	if (PatternGraph)
	{
		PatternGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	}
}

void FPCGExConnectorPatternEditor::InitEditor(
	UPCGExConnectorPatternAsset* InAsset,
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	EditedAsset = InAsset;

	EnsureGraphExists();
	BindGraphCommands();

	const TArray<UObject*> ObjectsToEdit = {InAsset};

	// Layout: graph on left (filling), details on right
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("PCGExConnectorPatternEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7f)
				->AddTab(GraphTabId, ETabState::OpenedTab)
				->SetForegroundTab(GraphTabId)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
		);

	InitAssetEditor(EToolkitMode::Standalone, InitToolkitHost, FName("PCGExConnectorPatternEditor"), Layout, true, true, ObjectsToEdit);

	// Toolbar
	TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FPCGExConnectorPatternEditor::BuildEditorToolbar)
	);
	AddToolbarExtender(ToolbarExtender);
	RegenerateMenusAndToolbars();
}

void FPCGExConnectorPatternEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FPCGExConnectorPatternEditor::SpawnGraphTab))
		.SetDisplayName(INVTEXT("Graph"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FPCGExConnectorPatternEditor::SpawnDetailsTab))
		.SetDisplayName(INVTEXT("Details"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FPCGExConnectorPatternEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(GraphTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FPCGExConnectorPatternEditor::SpawnGraphTab(const FSpawnTabArgs& Args)
{
	check(PatternGraph);

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FPCGExConnectorPatternEditor::OnSelectionChanged);

	GraphEditorWidget = SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.GraphToEdit(PatternGraph)
		.GraphEvents(GraphEvents)
		.IsEditable(true)
		.ShowGraphStateOverlay(false);

	return SNew(SDockTab)
		.TabRole(PanelTab)
		[
			GraphEditorWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPCGExConnectorPatternEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;

	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);

	// Show asset properties by default
	if (UPCGExConnectorPatternAsset* Asset = EditedAsset.Get())
	{
		DetailsView->SetObject(Asset);
	}

	return SNew(SDockTab)
		.TabRole(PanelTab)
		[
			DetailsView.ToSharedRef()
		];
}

void FPCGExConnectorPatternEditor::EnsureGraphExists()
{
	UPCGExConnectorPatternAsset* Asset = EditedAsset.Get();
	if (!Asset) { return; }

#if WITH_EDITORONLY_DATA
	if (!Asset->PatternGraph)
	{
		Asset->PatternGraph = NewObject<UPCGExConnectorPatternGraph>(Asset, NAME_None, RF_Transactional);
		Asset->PatternGraph->Schema = UPCGExConnectorPatternGraphSchema::StaticClass();

		UPCGExConnectorPatternGraph* Graph = Cast<UPCGExConnectorPatternGraph>(Asset->PatternGraph);
		Graph->OwningAsset = Asset;

		// Build from existing pattern data (legacy/first-open)
		if (Asset->Patterns.Num() > 0)
		{
			Graph->BuildGraphFromAsset();
		}
		else
		{
			// Create default root node
			Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		}

		Asset->MarkPackageDirty();
	}

	PatternGraph = Cast<UPCGExConnectorPatternGraph>(Asset->PatternGraph);
	if (PatternGraph)
	{
		PatternGraph->OwningAsset = Asset;

		// Bind graph change handler
		OnGraphChangedHandle = PatternGraph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateSP(this, &FPCGExConnectorPatternEditor::OnGraphChanged));

		// Resolve pins from current ConnectorSet
		PatternGraph->RebuildAllNodePins();
	}
#endif
}

void FPCGExConnectorPatternEditor::OnGraphChanged(const FEdGraphEditAction& Action)
{
	if (PatternGraph)
	{
		PatternGraph->CompileGraphToAsset();
	}
}

void FPCGExConnectorPatternEditor::OnSelectionChanged(const TSet<UObject*>& NewSelection)
{
	if (!DetailsView.IsValid()) { return; }

	TArray<UObject*> SelectedObjects = NewSelection.Array();

	if (SelectedObjects.Num() == 1)
	{
		// Show selected node properties
		DetailsView->SetObjects(SelectedObjects);
	}
	else if (SelectedObjects.Num() > 1)
	{
		DetailsView->SetObjects(SelectedObjects);
	}
	else
	{
		// No selection — show asset properties
		if (UPCGExConnectorPatternAsset* Asset = EditedAsset.Get())
		{
			DetailsView->SetObject(Asset);
		}
	}
}

void FPCGExConnectorPatternEditor::BuildEditorToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("PatternActions");
	{
		// Compile button
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						if (PatternGraph)
						{
							PatternGraph->CompileGraphToAsset();
						}
					})
			),
			NAME_None,
			FText::FromString(TEXT("Compile")),
			INVTEXT("Recompile all patterns from graph topology"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.Apply")
		);

		// Add Root button
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						if (!PatternGraph) { return; }

						const FScopedTransaction Transaction(INVTEXT("Add Pattern Root"));

						FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(*PatternGraph);
						UPCGExConnectorPatternGraphNode* RootNode = NodeCreator.CreateNode(true);
						RootNode->NodePosX = 400;
						RootNode->NodePosY = 0;
						RootNode->bIsPatternRoot = true;
						RootNode->PatternName = FName("NewPattern");
						NodeCreator.Finalize();

						PatternGraph->CompileGraphToAsset();
						PatternGraph->NotifyGraphChanged();
					})
			),
			NAME_None,
			FText::FromString(TEXT("Add Root")),
			INVTEXT("Add a new pattern root node"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
		);
	}
	ToolbarBuilder.EndSection();
}

void FPCGExConnectorPatternEditor::BindGraphCommands()
{
	GraphEditorCommands = MakeShared<FUICommandList>();

	// Delete
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CanDeleteNodes));

	// Copy
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CanCopyNodes));

	// Paste
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::PasteNodes),
		FCanExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CanPasteNodes));

	// Cut
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CanCopyNodes));

	// Duplicate (Ctrl+W in UE, also Ctrl+D via custom chord below)
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::DuplicateSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::CanCopyNodes));

	// Select All
	GraphEditorCommands->MapAction(
		FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::SelectAllNodes));

	// Create Comment (C key)
	GraphEditorCommands->MapAction(
		FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateSP(this, &FPCGExConnectorPatternEditor::OnCreateComment));
}

void FPCGExConnectorPatternEditor::DeleteSelectedNodes()
{
	if (!GraphEditorWidget.IsValid()) { return; }

	const FScopedTransaction Transaction(INVTEXT("Delete Selected Nodes"));
	PatternGraph->Modify();

	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	GraphEditorWidget->ClearSelectionSet();

	for (UObject* NodeObj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj);
		if (Node && Node->CanUserDeleteNode())
		{
			Node->Modify();
			Node->DestroyNode();
		}
	}

	PatternGraph->NotifyGraphChanged();
}

bool FPCGExConnectorPatternEditor::CanDeleteNodes() const
{
	if (!GraphEditorWidget.IsValid()) { return false; }

	for (UObject* NodeObj : GraphEditorWidget->GetSelectedNodes())
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
		{
			if (Node->CanUserDeleteNode()) { return true; }
		}
	}
	return false;
}

void FPCGExConnectorPatternEditor::CopySelectedNodes()
{
	if (!GraphEditorWidget.IsValid()) { return; }

	FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();

	// Remove non-duplicatable nodes
	for (auto It = SelectedNodes.CreateIterator(); It; ++It)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*It);
		if (!Node || !Node->CanDuplicateNode())
		{
			It.RemoveCurrent();
		}
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FPCGExConnectorPatternEditor::CanCopyNodes() const
{
	if (!GraphEditorWidget.IsValid()) { return false; }

	for (UObject* NodeObj : GraphEditorWidget->GetSelectedNodes())
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
		{
			if (Node->CanDuplicateNode()) { return true; }
		}
	}
	return false;
}

void FPCGExConnectorPatternEditor::PasteNodes()
{
	if (!GraphEditorWidget.IsValid() || !PatternGraph) { return; }

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	const FScopedTransaction Transaction(INVTEXT("Paste Nodes"));
	PatternGraph->Modify();

	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(PatternGraph, ClipboardContent, PastedNodes);

	// Compute center of pasted nodes
	FVector2D AvgPos = FVector2D::ZeroVector;
	for (const UEdGraphNode* Node : PastedNodes)
	{
		AvgPos.X += Node->NodePosX;
		AvgPos.Y += Node->NodePosY;
	}
	if (PastedNodes.Num() > 0)
	{
		AvgPos /= static_cast<double>(PastedNodes.Num());
	}

	// Offset to paste location
	const FVector2f PasteLocation = GraphEditorWidget->GetPasteLocation2f();
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->NodePosX = static_cast<int32>((Node->NodePosX - AvgPos.X) + PasteLocation.X);
		Node->NodePosY = static_cast<int32>((Node->NodePosY - AvgPos.Y) + PasteLocation.Y);
		Node->SnapToGrid(16);
	}

	// Select pasted nodes
	GraphEditorWidget->ClearSelectionSet();
	for (UEdGraphNode* Node : PastedNodes)
	{
		GraphEditorWidget->SetNodeSelection(Node, true);
	}

	PatternGraph->NotifyGraphChanged();
}

bool FPCGExConnectorPatternEditor::CanPasteNodes() const
{
	if (!GraphEditorWidget.IsValid() || !PatternGraph) { return false; }

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return !ClipboardContent.IsEmpty();
}

void FPCGExConnectorPatternEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	DeleteSelectedNodes();
}

void FPCGExConnectorPatternEditor::DuplicateSelectedNodes()
{
	CopySelectedNodes();
	PasteNodes();
}

void FPCGExConnectorPatternEditor::SelectAllNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->SelectAllNodes();
	}
}

void FPCGExConnectorPatternEditor::OnCreateComment()
{
	if (!GraphEditorWidget.IsValid() || !PatternGraph) { return; }

	const FScopedTransaction Transaction(INVTEXT("Create Comment"));
	PatternGraph->Modify();

	// Create comment node at current view location
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(PatternGraph);
	CommentNode->SetFlags(RF_Transactional);

	const FVector2D PasteLocation = GraphEditorWidget->GetPasteLocation2f();
	CommentNode->NodePosX = static_cast<int32>(PasteLocation.X);
	CommentNode->NodePosY = static_cast<int32>(PasteLocation.Y);
	CommentNode->NodeWidth = 400;
	CommentNode->NodeHeight = 200;
	CommentNode->NodeComment = TEXT("Comment");

	PatternGraph->AddNode(CommentNode, false, false);
	CommentNode->CreateNewGuid();
	CommentNode->PostPlacedNewNode();

	// Select it
	GraphEditorWidget->ClearSelectionSet();
	GraphEditorWidget->SetNodeSelection(CommentNode, true);

	PatternGraph->NotifyGraphChanged();
}

void FPCGExConnectorPatternEditor::SaveAsset_Execute()
{
	// Ensure compiled before save
	if (PatternGraph)
	{
		PatternGraph->CompileGraphToAsset();
	}
	FAssetEditorToolkit::SaveAsset_Execute();
}

#pragma endregion
