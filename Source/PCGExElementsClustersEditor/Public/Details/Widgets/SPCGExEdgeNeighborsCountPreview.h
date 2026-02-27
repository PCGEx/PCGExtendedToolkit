// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Utils/PCGExCompare.h"
#include "Filters/Edges/PCGExEdgeNeighborsCountFilter.h"

/**
 * Read-only 3-panel visualization for FPCGExEdgeNeighborsCountFilterConfig.
 * Shows how different neighbor count distributions evaluate under Sum/Any/Both modes.
 */
class PCGEXELEMENTSCLUSTERSEDITOR_API SPCGExEdgeNeighborsCountPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExEdgeNeighborsCountPreview)
			: _Mode(EPCGExRefineEdgeThresholdMode::Sum)
			, _Comparison(EPCGExComparison::StrictlyGreater)
			, _ThresholdConstant(2)
			, _Tolerance(0)
			, _bInvert(false)
			, _bShowThreshold(true)
		{
		}

		SLATE_ATTRIBUTE(EPCGExRefineEdgeThresholdMode, Mode)
		SLATE_ATTRIBUTE(EPCGExComparison, Comparison)
		SLATE_ATTRIBUTE(int32, ThresholdConstant)
		SLATE_ATTRIBUTE(int32, Tolerance)
		SLATE_ATTRIBUTE(bool, bInvert)
		SLATE_ATTRIBUTE(bool, bShowThreshold)
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
	TAttribute<EPCGExRefineEdgeThresholdMode> Mode;
	TAttribute<EPCGExComparison> Comparison;
	TAttribute<int32> ThresholdConstant;
	TAttribute<int32> Tolerance;
	TAttribute<bool> bInvert;
	TAttribute<bool> bShowThreshold;

	/** Evaluate filter result mirroring the actual Test() logic. */
	static bool Evaluate(EPCGExRefineEdgeThresholdMode InMode, EPCGExComparison InComparison, int32 FromCount, int32 ToCount, int32 Threshold, int32 InTolerance, bool bInInvert);

	/** Get display name for a mode value. */
	static FString GetModeName(EPCGExRefineEdgeThresholdMode InMode);

	/** Draw the muted "Per-Edge" attribute mode. */
	int32 PaintAttributeMode(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& LocalSize) const;

	/** Draw neighbor stubs fanning from an endpoint. */
	static void DrawNeighborStubs(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		int32 Count,
		bool bLeftSide);
};
