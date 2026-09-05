// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class FSpawnTabArgs;
class SDockTab;
class SWidget;

/**
 * FModeToolkit whose primary panel body FILLS the container instead of scrolling inside the host.
 *
 * The engine wraps GetInlineContent() in its own SScrollBox, and a scroll-box child can never fill height,
 * so a body with a fixed rail beside its own scrolled column cannot be inline content. This re-registers
 * the primary tab under its own spawner (FAssetEditorModeUILayer::SetModePanelInfo overwrites by tab id),
 * takes the engine's layout as-is (pinned palette over the inline holder) and puts MakeFillContent()
 * straight into the holder -- the one fill-height slot. GetInlineContent() answers null by contract so
 * UpdatePrimaryModePanel never re-wraps it.
 */
class PCGEXCOREEDITOR_API FPCGExModeToolkitBase : public FModeToolkit
{
public:
	//~ Begin FModeToolkit
	/** Null: the body arrives through MakeFillContent. Final, or the engine would scroll it again. */
	virtual TSharedPtr<SWidget> GetInlineContent() const final override;
	//~ End FModeToolkit

protected:
	//~ Begin FModeToolkit
	virtual void RequestModeUITabs() override;
	//~ End FModeToolkit

	/** The fill-height body under the pinned palette, asked for on every tab spawn. Return the SAME widget
	 *  each time (a respawn re-parents it); null leaves the holder collapsed. */
	virtual TSharedPtr<SWidget> MakeFillContent() = 0;

	/** Last word on the spawned tab, after the body is in place: wrap or decorate its content. */
	virtual void DecoratePrimaryTab(const TSharedRef<SDockTab>& InTab)
	{
	}

private:
	TSharedRef<SDockTab> SpawnPrimaryTab(const FSpawnTabArgs& Args);
};
