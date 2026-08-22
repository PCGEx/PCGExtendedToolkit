// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchEditorModeToolkit.h"

#include "Sketch/PCGExSketchEditorMode.h"
#include "Sketch/SPCGExSketchPanel.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "PCGExSketchEditorModeToolkit"

namespace PCGExSketchEditorModeToolkit
{
	const FName PanelPaletteName(TEXT("Sketch"));
}

void FPCGExSketchEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	EnsurePanelCreated();

	// The strip is collapsed at one palette, but the switcher still needs a selected segment to show
	// the palette widget at all.
	SetCurrentPalette(PCGExSketchEditorModeToolkit::PanelPaletteName);
}

FText FPCGExSketchEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Cluster Sketch");
}

void FPCGExSketchEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(PCGExSketchEditorModeToolkit::PanelPaletteName);
}

FText FPCGExSketchEditorModeToolkit::GetToolPaletteDisplayName(const FName Palette) const
{
	return LOCTEXT("PaletteSketch", "Sketch");
}

TSharedRef<SWidget> FPCGExSketchEditorModeToolkit::CreatePaletteWidget(TSharedPtr<FUICommandList> InCommandList, FName InToolbarCustomizationName, const FName InPaletteName)
{
	// Not the base's FUniformToolBarBuilder: its wrap-panel cells crush a multi-row widget, whereas the
	// palette switcher hosts a plain widget at full panel width.
	EnsurePanelCreated();
	return Panel.IsValid() ? Panel->MakeHeader() : SNullWidget::NullWidget;
}

TSharedPtr<SWidget> FPCGExSketchEditorModeToolkit::GetInlineContent() const
{
	const_cast<FPCGExSketchEditorModeToolkit*>(this)->EnsurePanelCreated();
	// The host wraps this in its own SScrollBox.
	return Panel.IsValid() ? Panel->MakeBody() : TSharedPtr<SWidget>();
}

void FPCGExSketchEditorModeToolkit::EnsurePanelCreated()
{
	if (Panel.IsValid())
	{
		return;
	}

	UPCGExSketchEditorMode* Mode = Cast<UPCGExSketchEditorMode>(GetScriptableEditorMode().Get());
	if (!Mode)
	{
		return;
	}

	TWeakObjectPtr<UPCGExSketchEditorMode> WeakMode = Mode;

	FPCGExSketchPanelContext Context;
	Context.ResolveActiveController = [WeakMode]() -> TSharedPtr<FPCGExSketchEditController>
	{
		UPCGExSketchEditorMode* Pinned = WeakMode.Get();
		return Pinned ? Pinned->GetActiveController() : nullptr;
	};
	Context.ResolveSketchObject = [WeakMode]() -> UObject*
	{
		UPCGExSketchEditorMode* Pinned = WeakMode.Get();
		return Pinned ? Pinned->GetActiveSketchObject() : nullptr;
	};
	Context.EnumerateSketches = [WeakMode](TArray<FPCGExSketchPanelEntry>& OutEntries)
	{
		if (UPCGExSketchEditorMode* Pinned = WeakMode.Get())
		{
			Pinned->EnumerateSketches(OutEntries);
		}
	};
	Context.SetActiveSketch = [WeakMode](const TSharedPtr<FPCGExSketchEditController>& InController)
	{
		if (UPCGExSketchEditorMode* Pinned = WeakMode.Get())
		{
			Pinned->SetActiveController(InController);
		}
	};
	Context.RequestBodyRefresh = FSimpleDelegate::CreateLambda([WeakMode]()
	{
		if (UPCGExSketchEditorMode* Pinned = WeakMode.Get())
		{
			Pinned->RequestViewportRefresh();
		}
	});

	SAssignNew(Panel, SPCGExSketchPanel, Context);
}

#undef LOCTEXT_NAMESPACE
