// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FEditorModeTools;
class FEditorViewportClient;
class SLevelViewport;
class SWidget;

/**
 * Parents one Slate widget onto the level viewport under the cursor, and follows the cursor across
 * viewports. An editor mode has N level viewports and no host of its own, so an in-viewport overlay must
 * ride whichever one is being used: Update() each tick re-parents the SAME widget instance when the
 * hovered viewport changes and keeps the current one when nothing is hovered (the cursor is on the
 * overlay itself). The widget keeps its state across the move.
 *
 * Hover, not focus: GCurrentLevelEditingViewportClient tracks the last CLICKED viewport, which would
 * strand the overlay while the user works in another. The downcast to SLevelViewport is proven by
 * membership in GEditor's typed level-viewport-client array.
 *
 * A full-viewport widget must set itself SelfHitTestInvisible, or it swallows every mouse move the
 * viewport's hover needs.
 */
class PCGEXCOREEDITOR_API FPCGExLevelViewportOverlayHost
{
public:
	/** The level viewport under the cursor, or null (a non-level viewport, or no hover). */
	static TSharedPtr<SLevelViewport> ResolveHoveredLevelViewport(const FEditorModeTools* ModeManager);

	/** Re-host InWidget on the hovered level viewport when it changed. @return true when the widget was
	 *  moved onto a different viewport this call (its screen-space caches are now foreign). */
	bool Update(const FEditorModeTools* ModeManager, const TSharedRef<SWidget>& InWidget);

	/** Detach from the current viewport, if any. */
	void Remove();

	/** Whether Client is the viewport currently hosting the widget. */
	bool IsHostedOn(const FEditorViewportClient* Client) const;

	TSharedPtr<SLevelViewport> GetViewport() const;

private:
	TSharedPtr<SWidget> Hosted;
	TWeakPtr<SLevelViewport> Viewport;
};
