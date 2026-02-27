// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Utils/PCGExCompare.h"

/**
 * Read-only 3-panel visualization for FPCGExEdgeEndpointsCompareNumFilterConfig.
 * Shows how different Start vs End value relationships evaluate under the current comparison operator.
 */
class PCGEXELEMENTSCLUSTERSEDITOR_API SPCGExEdgeEndpointsCompareNumPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExEdgeEndpointsCompareNumPreview)
			: _Comparison(EPCGExComparison::StrictlyGreater)
			, _bInvert(false)
		{
		}

		SLATE_ATTRIBUTE(EPCGExComparison, Comparison)
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
	TAttribute<EPCGExComparison> Comparison;
	TAttribute<bool> bInvert;
};
