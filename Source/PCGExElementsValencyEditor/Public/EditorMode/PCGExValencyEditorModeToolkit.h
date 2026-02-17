// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"

class UPCGExValencyCageEditorMode;

/**
 * Command bindings for the Valency editor mode.
 * Registered in module startup, mapped in toolkit Init.
 */
class FValencyEditorCommands : public TCommands<FValencyEditorCommands>
{
public:
	FValencyEditorCommands()
		: TCommands<FValencyEditorCommands>(
			TEXT("ValencyEditor"),
			NSLOCTEXT("Contexts", "ValencyEditor", "Valency Editor"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> CleanupConnections;

	TSharedPtr<FUICommandInfo> AddConnector;
	TSharedPtr<FUICommandInfo> RemoveConnector;
	TSharedPtr<FUICommandInfo> DuplicateConnector;
	TSharedPtr<FUICommandInfo> CycleConnectorPolarity;
};

/**
 * Root panel widget for the Valency editor mode side panel.
 * Contains all sub-sections in a scrollable layout:
 * - Visualization toggles
 * - Scene bar (compact context/rebuild controls)
 * - Module info (context-sensitive cage/volume/palette properties)
 * - Control tabs (Connectors / Assets / Placement)
 * - Validation messages
 */
class SValencyModePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencyModePanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Get the editor mode this panel operates on */
	UPCGExValencyCageEditorMode* GetEditorMode() const { return EditorMode; }

	/** Rebuild all panel sections (called when scene changes) */
	void RefreshPanel();

private:
	/** The editor mode we're attached to */
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Root scroll box containing all sections */
	TSharedPtr<SScrollBox> ScrollBox;

	/** Sub-widgets (populated incrementally by phases) */
	TSharedPtr<SWidget> VisTogglesWidget;
	TSharedPtr<class SValencySceneBar> SceneBarWidget;
	TSharedPtr<class SValencyModuleInfo> ModuleInfoWidget;
	TSharedPtr<class SValencyControlTabs> ControlTabsWidget;
	TSharedPtr<SWidget> ValidationWidget;

	/** Rebuild the scroll box contents */
	void RebuildLayout();
};

/**
 * Mode toolkit for the Valency editor mode.
 * Creates and manages the side panel that appears when the mode is active.
 */
class PCGEXELEMENTSVALENCYEDITOR_API FPCGExValencyEditorModeToolkit : public FModeToolkit
{
public:
	FPCGExValencyEditorModeToolkit();
	virtual ~FPCGExValencyEditorModeToolkit() override;

	//~ Begin FModeToolkit Interface
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;
	//~ End FModeToolkit Interface

	/** Ensure the panel widget is created (lazy initialization) */
	void EnsurePanelCreated();

	/** Get the panel widget */
	TSharedPtr<SValencyModePanel> GetPanelWidget() const { return PanelWidget; }

private:
	/** The root panel widget */
	TSharedPtr<SValencyModePanel> PanelWidget;
};
