// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' FPCGExRampsEditorStyle -- keep the two in sync.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Slate style set for the inline property curve editor. Registers the (white, tint-at-draw-time)
 * SVG brushes used for graph key markers and strip slider gems.
 *
 * Brushes are registered only when their source .svg is actually present under the plugin's Resources
 * folder, so the widgets fall back to a drawn shape until the artwork is supplied (GetKeyBrush /
 * GetSliderBrush return nullptr when the file is missing). Owned by FPCGExPropertiesEditorModule.
 */
class FPCGExPropertiesEditorStyle : public FSlateStyleSet
{
public:
	FPCGExPropertiesEditorStyle();
	virtual ~FPCGExPropertiesEditorStyle();

	static FName StaticStyleSetName();

	/** Graph key-marker brush, or nullptr when Resources/RampKey.svg is absent (caller draws a fallback). */
	static const FSlateBrush* GetKeyBrush();
	/** Strip slider-gem brush, or nullptr when Resources/RampSlider.svg is absent (caller draws a fallback). */
	static const FSlateBrush* GetSliderBrush();

private:
	/** Registered brush by name from this style, or nullptr if unregistered (source file was missing). */
	static const FSlateBrush* FindBrush(const FName BrushName);
};
