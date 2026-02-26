// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Utils/PCGExCompare.h"

/**
 * Read-only arc visualization for dot product comparison structs.
 * Shows pass/fail angular regions, threshold line, and tolerance bands.
 */
class PCGEXCOREEDITOR_API SPCGExDotComparisonPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExDotComparisonPreview)
			: _Comparison(EPCGExComparison::EqualOrGreater)
			, _bUnsigned(false)
			, _ComparisonThreshold(0.5)
			, _ComparisonTolerance(0.0)
			, _bShowThreshold(true)
		{
		}

		SLATE_ATTRIBUTE(EPCGExComparison, Comparison)
		SLATE_ATTRIBUTE(bool, bUnsigned)
		SLATE_ATTRIBUTE(double, ComparisonThreshold)
		SLATE_ATTRIBUTE(double, ComparisonTolerance)
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
	TAttribute<EPCGExComparison> Comparison;
	TAttribute<bool> bUnsigned;
	TAttribute<double> ComparisonThreshold;
	TAttribute<double> ComparisonTolerance;
	TAttribute<bool> bShowThreshold;

	/** Evaluate pass/fail for a given input dot product using the pre-computed comparison state. */
	static bool Evaluate(
		EPCGExComparison Op,
		double InputDot,
		double InComparisonThreshold,
		double InComparisonTolerance,
		bool bInUnsigned);

	/** Draw a filled arc fan using custom vertices. */
	void DrawArcFan(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		double StartAngle,
		double EndAngle,
		double Radius,
		const FLinearColor& Color,
		int32 NumSegments) const;

	/** Draw a polyline arc outline. */
	void DrawArcOutline(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		double StartAngle,
		double EndAngle,
		double Radius,
		const FLinearColor& Color,
		float Thickness,
		int32 NumSegments) const;

	/** Draw a radial line from center at the given vector angle. */
	void DrawRadialLine(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		double VectorAngle,
		double Radius,
		const FLinearColor& Color,
		float Thickness) const;

	/** Convert a vector angle (radians, 0=same dir) to a screen point. */
	static FVector2D AngleToScreen(const FVector2D& Center, double VectorAngleRad, double Radius);

	static constexpr int32 ArcSegments = 64;
	static constexpr float Padding = 10.0f;
	static constexpr float DesiredWidth = 240.0f;
	static constexpr float LabelMargin = 18.0f;
	static constexpr float DesiredHeight = 140.0f;
};
