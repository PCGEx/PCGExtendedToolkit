// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Utils/PCGExCompare.h"
#include "Math/PCGExMathMean.h"
#include "Filters/PCGExAdjacency.h"

/**
 * Read-only star-diagram visualization for FPCGExAdjacencySettings.
 * Shows 3 side-by-side panels illustrating different adjacency outcomes
 * based on the current mode, threshold, and consolidation settings.
 */
class PCGEXELEMENTSCLUSTERSEDITOR_API SPCGExAdjacencyPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExAdjacencyPreview)
			: _Mode(EPCGExAdjacencyTestMode::Some)
			, _Consolidation(EPCGExAdjacencyGatherMode::Individual)
			, _ThresholdComparison(EPCGExComparison::NearlyEqual)
			, _ThresholdType(EPCGExMeanMeasure::Discrete)
			, _DiscreteThreshold(1)
			, _RelativeThreshold(0.5)
			, _Rounding(EPCGExRelativeThresholdRoundingMode::Round)
			, _ThresholdTolerance(0)
			, _bShowThreshold(true)
		{
		}

		SLATE_ATTRIBUTE(EPCGExAdjacencyTestMode, Mode)
		SLATE_ATTRIBUTE(EPCGExAdjacencyGatherMode, Consolidation)
		SLATE_ATTRIBUTE(EPCGExComparison, ThresholdComparison)
		SLATE_ATTRIBUTE(EPCGExMeanMeasure, ThresholdType)
		SLATE_ATTRIBUTE(int32, DiscreteThreshold)
		SLATE_ATTRIBUTE(double, RelativeThreshold)
		SLATE_ATTRIBUTE(EPCGExRelativeThresholdRoundingMode, Rounding)
		SLATE_ATTRIBUTE(int32, ThresholdTolerance)
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
	TAttribute<EPCGExAdjacencyTestMode> Mode;
	TAttribute<EPCGExAdjacencyGatherMode> Consolidation;
	TAttribute<EPCGExComparison> ThresholdComparison;
	TAttribute<EPCGExMeanMeasure> ThresholdType;
	TAttribute<int32> DiscreteThreshold;
	TAttribute<double> RelativeThreshold;
	TAttribute<EPCGExRelativeThresholdRoundingMode> Rounding;
	TAttribute<int32> ThresholdTolerance;
	TAttribute<bool> bShowThreshold;

	/** Draw the "Some" mode panels — threshold-based counting. */
	int32 PaintSomeMode(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& LocalSize,
		int32 Threshold,
		EPCGExComparison Comparison,
		int32 Tolerance) const;

	/** Draw the "All + Individual" mode panels. */
	int32 PaintAllIndividualMode(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& LocalSize) const;

	/** Draw the "All + Aggregated" mode panels (Average/Min/Max/Sum). */
	int32 PaintAllAggregatedMode(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& LocalSize,
		EPCGExAdjacencyGatherMode GatherMode) const;

	/** Draw the muted "Per-Point" attribute mode. */
	int32 PaintAttributeMode(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& LocalSize) const;

	/** Draw a single star panel with green/red branches. */
	void DrawStarPanel(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& Center,
		double Radius,
		int32 TotalBranches,
		int32 GreenCount,
		bool bOverallPass) const;

	/** Draw a single aggregated star panel with value-proportional branches. */
	void DrawAggregatedPanel(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& AllottedGeometry,
		int32 LayerId,
		const FVector2D& Center,
		double MaxRadius,
		const TArray<double>& Values,
		int32 HighlightIndex,
		EPCGExAdjacencyGatherMode GatherMode) const;

	/** Draw a filled circle using custom vertices. */
	void DrawFilledCircle(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		double Radius,
		const FLinearColor& Color,
		int32 NumSegments = 16) const;

	/** Draw a filled rectangle for panel background. */
	void DrawFilledRect(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& TopLeft,
		const FVector2D& Size,
		const FLinearColor& Color) const;

	/** Compute the effective threshold from current settings. */
	int32 ComputeThreshold(int32 TotalNeighbors) const;

	static constexpr float DesiredHeight = 140.0f;
	static constexpr float Padding = 6.0f;
	static constexpr float PanelGap = 6.0f;
	static constexpr float MaxStarRadius = 30.0f;
};
