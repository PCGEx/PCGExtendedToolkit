// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class SPCGExSketchPanel;

/**
 * Mode toolkit for the in-level sketch mode: the seam that gets SPCGExSketchPanel into the mode panel.
 *
 * Exactly one palette, by design: the host collapses the palette tab strip at one entry, leaving the
 * pinned area to the panel's header and the scroll box to its body. Two palettes add a competing tab
 * strip; an empty list stops CreatePaletteWidget being called at all.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchEditorModeToolkit : public FModeToolkit
{
public:
	//~ Begin FModeToolkit
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;

	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExSketchEditorModeToolkit");
	}

	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;
	virtual void GetToolPaletteNames(TArray<FName>& PaletteNames) const override;
	virtual FText GetToolPaletteDisplayName(FName Palette) const override;
	//~ End FModeToolkit

protected:
	virtual TSharedRef<SWidget> CreatePaletteWidget(TSharedPtr<FUICommandList> InCommandList, FName InToolbarCustomizationName, FName InPaletteName) override;

private:
	void EnsurePanelCreated();

	TSharedPtr<SPCGExSketchPanel> Panel;
};
