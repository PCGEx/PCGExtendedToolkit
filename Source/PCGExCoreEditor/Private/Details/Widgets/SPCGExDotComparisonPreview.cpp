// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Widgets/SPCGExDotComparisonPreview.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PCGExDotPreviewConstants
{
	static const FLinearColor PassColor(0.1f, 0.6f, 0.2f, 0.5f);
	static const FLinearColor FailColor(0.15f, 0.05f, 0.05f, 0.4f);
	static const FLinearColor ThresholdColor(1.0f, 1.0f, 1.0f, 0.9f);
	static const FLinearColor ToleranceColor(1.0f, 1.0f, 0.3f, 0.6f);
	static const FLinearColor ArcOutlineColor(0.5f, 0.5f, 0.5f, 0.6f);
	static const FLinearColor ReferenceArrowColor(0.7f, 0.7f, 1.0f, 0.9f);
	static const FLinearColor LabelColor(0.6f, 0.6f, 0.6f, 0.8f);
	static const FLinearColor AttributeModeColor(0.3f, 0.3f, 0.3f, 0.3f);
}

void SPCGExDotComparisonPreview::Construct(const FArguments& InArgs)
{
	Comparison = InArgs._Comparison;
	bUnsigned = InArgs._bUnsigned;
	ComparisonThreshold = InArgs._ComparisonThreshold;
	ComparisonTolerance = InArgs._ComparisonTolerance;
	bShowThreshold = InArgs._bShowThreshold;
}

FVector2D SPCGExDotComparisonPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}

FVector2D SPCGExDotComparisonPreview::AngleToScreen(const FVector2D& Center, const double VectorAngleRad, const double Radius)
{
	// 0 = up (same direction), PI/2 = right (perpendicular), PI = down (opposite)
	return Center + FVector2D(FMath::Sin(VectorAngleRad) * Radius, -FMath::Cos(VectorAngleRad) * Radius);
}

bool SPCGExDotComparisonPreview::Evaluate(
	const EPCGExComparison Op,
	const double InputDot,
	const double InComparisonThreshold,
	const double InComparisonTolerance,
	const bool bInUnsigned)
{
	const double InputRemapped = bInUnsigned ? FMath::Abs(InputDot) : (1.0 + InputDot) * 0.5;
	return PCGExCompare::Compare(Op, InputRemapped, InComparisonThreshold, InComparisonTolerance);
}

void SPCGExDotComparisonPreview::DrawArcFan(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const double StartAngle,
	const double EndAngle,
	const double Radius,
	const FLinearColor& Color,
	const int32 NumSegments) const
{
	if (NumSegments < 1 || FMath::IsNearlyEqual(StartAngle, EndAngle)) { return; }

	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor VertColor = Color.ToFColor(true);
	const FColor NoColor(0, 0, 0, 0);

	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;

	Vertices.Reserve(NumSegments + 2);
	Indices.Reserve(NumSegments * 3);

	// Center vertex (index 0)
	Vertices.Add(FSlateVertex::Make(
		RenderTransform, FVector2f(Center), FVector2f::ZeroVector, VertColor, NoColor));

	// Arc vertices
	const double AngleStep = (EndAngle - StartAngle) / NumSegments;
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const double Angle = StartAngle + AngleStep * i;
		const FVector2D ScreenPos = AngleToScreen(Center, Angle, Radius);

		Vertices.Add(FSlateVertex::Make(
			RenderTransform, FVector2f(ScreenPos), FVector2f::ZeroVector, VertColor, NoColor));
	}

	// Build triangle fan
	for (int32 i = 0; i < NumSegments; ++i)
	{
		Indices.Add(0);
		Indices.Add(i + 1);
		Indices.Add(i + 2);
	}

	const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
		*FCoreStyle::Get().GetDefaultBrush());

	FSlateDrawElement::MakeCustomVerts(
		OutDrawElements,
		LayerId,
		ResourceHandle,
		Vertices,
		Indices,
		nullptr, 0, 0);
}

void SPCGExDotComparisonPreview::DrawArcOutline(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const double StartAngle,
	const double EndAngle,
	const double Radius,
	const FLinearColor& Color,
	const float Thickness,
	const int32 NumSegments) const
{
	TArray<FVector2D> Points;
	Points.Reserve(NumSegments + 1);

	const double AngleStep = (EndAngle - StartAngle) / NumSegments;
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const double Angle = StartAngle + AngleStep * i;
		Points.Add(AngleToScreen(Center, Angle, Radius));
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Color,
		true,
		Thickness);
}

void SPCGExDotComparisonPreview::DrawRadialLine(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const double VectorAngle,
	const double Radius,
	const FLinearColor& Color,
	const float Thickness) const
{
	const FVector2D EndPoint = AngleToScreen(Center, VectorAngle, Radius);

	TArray<FVector2D> LinePoints;
	LinePoints.Add(Center);
	LinePoints.Add(EndPoint);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		LinePoints,
		ESlateDrawEffect::None,
		Color,
		true,
		Thickness);
}

int32 SPCGExDotComparisonPreview::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	// Read current attribute values
	const EPCGExComparison CurrentComparison = Comparison.Get();
	const bool bCurrentUnsigned = bUnsigned.Get();
	const double CurrentThreshold = ComparisonThreshold.Get();
	const double CurrentTolerance = ComparisonTolerance.Get();
	const bool bCurrentShowThreshold = bShowThreshold.Get();

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	// Layout: always a full circle centered in the widget.
	// Reference vector always points UP (0°).
	// Left half always mirrors right half. Unsigned additionally mirrors vertically.
	const FVector2D ArcCenter(LocalSize.X * 0.5, LocalSize.Y * 0.5);
	double ArcRadius = FMath::Min(LocalSize.X * 0.5 - LabelMargin, LocalSize.Y * 0.5 - LabelMargin);
	ArcRadius = FMath::Max(ArcRadius, 10.0);

	// --- Attribute mode: muted gray, no detail ---
	if (!bCurrentShowThreshold)
	{
		// Full circle fill
		DrawArcFan(OutDrawElements, LayerId, AllottedGeometry, ArcCenter, 0.0, UE_PI, ArcRadius, PCGExDotPreviewConstants::AttributeModeColor, ArcSegments);
		DrawArcFan(OutDrawElements, LayerId, AllottedGeometry, ArcCenter, -UE_PI, 0.0, ArcRadius, PCGExDotPreviewConstants::AttributeModeColor, ArcSegments);

		// Full circle outline
		DrawArcOutline(OutDrawElements, LayerId + 1, AllottedGeometry, ArcCenter, -UE_PI, UE_PI, ArcRadius, PCGExDotPreviewConstants::ArcOutlineColor, 1.0f, ArcSegments * 2);

		// Reference arrow (always up)
		DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, 0.0, ArcRadius + 5.0, PCGExDotPreviewConstants::ReferenceArrowColor, 1.5f);

		// "Per-Point" label at center
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const FVector2D TextPos(ArcCenter.X - 24.0, ArcCenter.Y - 6.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 16), FSlateLayoutTransform(TextPos)),
			TEXT("Per-Point"),
			Font, ESlateDrawEffect::None,
			PCGExDotPreviewConstants::LabelColor);

		return LayerId + 4;
	}

	// --- Normal mode: batch pass/fail triangles ---

	const bool bHasTolerance = (CurrentComparison == EPCGExComparison::NearlyEqual || CurrentComparison == EPCGExComparison::NearlyNotEqual);

	{
		const FSlateRenderTransform& RT = AllottedGeometry.GetAccumulatedRenderTransform();
		const FColor PassVC = PCGExDotPreviewConstants::PassColor.ToFColor(true);
		const FColor FailVC = PCGExDotPreviewConstants::FailColor.ToFColor(true);

		// Always draw both halves (right + left mirror)
		const int32 MaxTris = ArcSegments * 2;
		TArray<FSlateVertex> PassVerts, FailVerts;
		TArray<SlateIndex> PassIdx, FailIdx;
		PassVerts.Reserve(MaxTris * 3);
		FailVerts.Reserve(MaxTris * 3);
		PassIdx.Reserve(MaxTris * 3);
		FailIdx.Reserve(MaxTris * 3);

		const FColor NoCol(0, 0, 0, 0);

		auto EmitTri = [&RT, &NoCol](TArray<FSlateVertex>& V, TArray<SlateIndex>& I,
		                              const FVector2D& A, const FVector2D& B, const FVector2D& C, const FColor& Col)
		{
			const SlateIndex Base = static_cast<SlateIndex>(V.Num());
			V.Add(FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col, NoCol));
			V.Add(FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col, NoCol));
			V.Add(FSlateVertex::Make(RT, FVector2f(C), FVector2f::ZeroVector, Col, NoCol));
			I.Add(Base);
			I.Add(Base + 1);
			I.Add(Base + 2);
		};

		// Right half: sweep 0 → PI (top → right → bottom)
		// dot = cos(θ): 1 at 0°, 0 at 90°, -1 at 180°
		// Evaluate handles unsigned internally via abs(dot), so the bottom half
		// naturally mirrors the top half when unsigned is on.
		const double AngleStep = UE_PI / ArcSegments;
		for (int32 i = 0; i < ArcSegments; ++i)
		{
			const double A0 = AngleStep * i;
			const double A1 = AngleStep * (i + 1);
			const double MidDot = FMath::Cos((A0 + A1) * 0.5);

			const bool bPass = Evaluate(CurrentComparison, MidDot, CurrentThreshold, CurrentTolerance, bCurrentUnsigned);
			auto& TV = bPass ? PassVerts : FailVerts;
			auto& TI = bPass ? PassIdx : FailIdx;
			const FColor& TC = bPass ? PassVC : FailVC;

			// Right half triangle
			EmitTri(TV, TI, ArcCenter,
			        AngleToScreen(ArcCenter, A0, ArcRadius),
			        AngleToScreen(ArcCenter, A1, ArcRadius), TC);

			// Left half mirror (same evaluation since cos(-θ) = cos(θ))
			EmitTri(TV, TI, ArcCenter,
			        AngleToScreen(ArcCenter, -A1, ArcRadius),
			        AngleToScreen(ArcCenter, -A0, ArcRadius), TC);
		}

		const FSlateResourceHandle RH = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
			*FCoreStyle::Get().GetDefaultBrush());

		if (FailVerts.Num() > 0)
		{
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, RH, FailVerts, FailIdx, nullptr, 0, 0);
		}
		if (PassVerts.Num() > 0)
		{
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, RH, PassVerts, PassIdx, nullptr, 0, 0);
		}
	}

	// --- Full circle outline ---
	DrawArcOutline(OutDrawElements, LayerId + 1, AllottedGeometry, ArcCenter, -UE_PI, UE_PI, ArcRadius, PCGExDotPreviewConstants::ArcOutlineColor, 1.0f, ArcSegments * 2);

	// --- Reference arrow at 0° (always pointing UP) ---
	{
		const double ArrowExtend = ArcRadius + 8.0;
		DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, 0.0, ArrowExtend, PCGExDotPreviewConstants::ReferenceArrowColor, 1.5f);

		// Arrowhead (V-shape)
		const FVector2D Tip = AngleToScreen(ArcCenter, 0.0, ArrowExtend);
		TArray<FVector2D> ArrowHead;
		ArrowHead.Add(Tip + FVector2D(-3.0, 6.0));
		ArrowHead.Add(Tip);
		ArrowHead.Add(Tip + FVector2D(3.0, 6.0));
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			ArrowHead, ESlateDrawEffect::None,
			PCGExDotPreviewConstants::ReferenceArrowColor, true, 1.5f);
	}

	// --- Threshold radial line ---
	{
		// Convert threshold from comparison space back to vector angle
		// For unsigned: threshold = abs(dot), so dot = threshold, angle = acos(threshold)
		// For signed: threshold = (1+dot)*0.5, so dot = 2*threshold - 1, angle = acos(2*threshold - 1)
		double ThresholdDot;
		if (bCurrentUnsigned)
		{
			ThresholdDot = FMath::Clamp(CurrentThreshold, 0.0, 1.0);
		}
		else
		{
			ThresholdDot = FMath::Clamp(CurrentThreshold * 2.0 - 1.0, -1.0, 1.0);
		}

		const double ThresholdAngle = FMath::Acos(ThresholdDot);

		// Right side threshold
		DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, ThresholdAngle, ArcRadius + 3.0, PCGExDotPreviewConstants::ThresholdColor, 2.0f);
		// Left side mirror
		DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, -ThresholdAngle, ArcRadius + 3.0, PCGExDotPreviewConstants::ThresholdColor, 2.0f);

		// --- Tolerance band lines (for ~= and !~= modes) ---
		if (bHasTolerance && CurrentTolerance > 0.001)
		{
			const double TolLowComp = FMath::Clamp(CurrentThreshold - CurrentTolerance, 0.0, 1.0);
			const double TolHighComp = FMath::Clamp(CurrentThreshold + CurrentTolerance, 0.0, 1.0);

			double TolLowDot, TolHighDot;
			if (bCurrentUnsigned)
			{
				TolLowDot = FMath::Clamp(TolLowComp, 0.0, 1.0);
				TolHighDot = FMath::Clamp(TolHighComp, 0.0, 1.0);
			}
			else
			{
				TolLowDot = FMath::Clamp(TolLowComp * 2.0 - 1.0, -1.0, 1.0);
				TolHighDot = FMath::Clamp(TolHighComp * 2.0 - 1.0, -1.0, 1.0);
			}

			const double TolLowAngle = FMath::Acos(TolHighDot);  // Higher dot = smaller angle
			const double TolHighAngle = FMath::Acos(TolLowDot);  // Lower dot = larger angle

			// Right side tolerance lines
			DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, TolLowAngle, ArcRadius + 2.0, PCGExDotPreviewConstants::ToleranceColor, 1.0f);
			DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, TolHighAngle, ArcRadius + 2.0, PCGExDotPreviewConstants::ToleranceColor, 1.0f);
			// Left side mirror
			DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, -TolLowAngle, ArcRadius + 2.0, PCGExDotPreviewConstants::ToleranceColor, 1.0f);
			DrawRadialLine(OutDrawElements, LayerId + 2, AllottedGeometry, ArcCenter, -TolHighAngle, ArcRadius + 2.0, PCGExDotPreviewConstants::ToleranceColor, 1.0f);
		}

		// --- Threshold angle label (on right side only to avoid clutter) ---
		{
			const double LabelDegrees = FMath::RadiansToDegrees(ThresholdAngle);
			const FString LabelStr = FString::Printf(TEXT("%.0f%s"), LabelDegrees, TEXT("\u00B0"));
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

			const FVector2D LabelPos = AngleToScreen(ArcCenter, ThresholdAngle, ArcRadius + 12.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D(40, 14), FSlateLayoutTransform(LabelPos - FVector2D(0, 7))),
				LabelStr,
				Font, ESlateDrawEffect::None,
				PCGExDotPreviewConstants::ThresholdColor);
		}
	}

	// --- Angle labels at key positions ---
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

		// 0° label (top - same direction)
		{
			const FVector2D Pos = AngleToScreen(ArcCenter, 0.0, ArcRadius + 4.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D(16, 12), FSlateLayoutTransform(Pos - FVector2D(8, 14))),
				TEXT("0\u00B0"),
				Font, ESlateDrawEffect::None,
				PCGExDotPreviewConstants::LabelColor);
		}

		// 90° label (right - perpendicular)
		{
			const FVector2D Pos = AngleToScreen(ArcCenter, UE_HALF_PI, ArcRadius + 4.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D(24, 12), FSlateLayoutTransform(Pos + FVector2D(2, -6))),
				TEXT("90\u00B0"),
				Font, ESlateDrawEffect::None,
				PCGExDotPreviewConstants::LabelColor);
		}

		// 180° label (bottom - opposite)
		{
			const FVector2D Pos = AngleToScreen(ArcCenter, UE_PI, ArcRadius + 4.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D(30, 12), FSlateLayoutTransform(Pos - FVector2D(15, -2))),
				TEXT("180\u00B0"),
				Font, ESlateDrawEffect::None,
				PCGExDotPreviewConstants::LabelColor);
		}

		// 90° label (left - perpendicular, mirror)
		{
			const FVector2D Pos = AngleToScreen(ArcCenter, -UE_HALF_PI, ArcRadius + 4.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D(24, 12), FSlateLayoutTransform(Pos - FVector2D(26, 6))),
				TEXT("90\u00B0"),
				Font, ESlateDrawEffect::None,
				PCGExDotPreviewConstants::LabelColor);
		}
	}

	return LayerId + 4;
}
