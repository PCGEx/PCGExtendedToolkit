// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Filters/Edges/PCGExEdgeEndpointsCheckFilter.h"

/**
 * Read-only 4-panel truth table visualization for FPCGExEdgeEndpointsCheckFilterConfig.
 * Shows all endpoint pass/fail combinations and how the current mode evaluates them.
 */
class PCGEXELEMENTSCLUSTERSEDITOR_API SPCGExEdgeEndpointsCheckPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExEdgeEndpointsCheckPreview)
			: _Mode(EPCGExEdgeEndpointsCheckMode::Both)
			, _Expects(EPCGExFilterResult::Pass)
			, _bInvert(false)
		{
		}

		SLATE_ATTRIBUTE(EPCGExEdgeEndpointsCheckMode, Mode)
		SLATE_ATTRIBUTE(EPCGExFilterResult, Expects)
		SLATE_ATTRIBUTE(bool, bInvert)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TAttribute<EPCGExEdgeEndpointsCheckMode> Mode;
	TAttribute<EPCGExFilterResult> Expects;
	TAttribute<bool> bInvert;

	/** Evaluate filter result mirroring the actual Test() logic. */
	static bool Evaluate(EPCGExEdgeEndpointsCheckMode InMode, EPCGExFilterResult InExpects, bool bInInvert, bool bStartRaw, bool bEndRaw);

	/** Get display name for a mode value. */
	static FString GetModeName(EPCGExEdgeEndpointsCheckMode InMode);
};
