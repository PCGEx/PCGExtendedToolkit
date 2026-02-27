// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Widgets/SPCGExEdgeNeighborsCountPreview.h"
#include "Details/Widgets/PCGExEdgeFilterPreviewHelpers.h"

void SPCGExEdgeNeighborsCountPreview::Construct(const FArguments& InArgs)
{
	Mode = InArgs._Mode;
	Comparison = InArgs._Comparison;
	ThresholdConstant = InArgs._ThresholdConstant;
	Tolerance = InArgs._Tolerance;
	bInvert = InArgs._bInvert;
	bShowThreshold = InArgs._bShowThreshold;
}

FVector2D SPCGExEdgeNeighborsCountPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(1.0f, PCGExEdgeFilterPreview::DesiredHeight);
}

bool SPCGExEdgeNeighborsCountPreview::Evaluate(
	const EPCGExRefineEdgeThresholdMode InMode,
	const EPCGExComparison InComparison,
	const int32 FromCount,
	const int32 ToCount,
	const int32 Threshold,
	const int32 InTolerance,
	const bool bInInvert)
{
	bool bPass;
	switch (InMode)
	{
	case EPCGExRefineEdgeThresholdMode::Sum:
		bPass = PCGExCompare::Compare(InComparison, FromCount + ToCount, Threshold, static_cast<double>(InTolerance));
		break;
	case EPCGExRefineEdgeThresholdMode::Any:
		bPass = PCGExCompare::Compare(InComparison, FromCount, Threshold, static_cast<double>(InTolerance)) ||
			PCGExCompare::Compare(InComparison, ToCount, Threshold, static_cast<double>(InTolerance));
		break;
	case EPCGExRefineEdgeThresholdMode::Both:
		bPass = PCGExCompare::Compare(InComparison, FromCount, Threshold, static_cast<double>(InTolerance)) &&
			PCGExCompare::Compare(InComparison, ToCount, Threshold, static_cast<double>(InTolerance));
		break;
	default:
		bPass = false;
		break;
	}

	return bInInvert ? !bPass : bPass;
}

FString SPCGExEdgeNeighborsCountPreview::GetModeName(const EPCGExRefineEdgeThresholdMode InMode)
{
	switch (InMode)
	{
	case EPCGExRefineEdgeThresholdMode::Sum: return TEXT("Sum");
	case EPCGExRefineEdgeThresholdMode::Any: return TEXT("Any");
	case EPCGExRefineEdgeThresholdMode::Both: return TEXT("Both");
	default: return TEXT("?");
	}
}

void SPCGExEdgeNeighborsCountPreview::DrawNeighborStubs(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const int32 Count,
	const bool bLeftSide)
{
	using namespace PCGExEdgeFilterPreview;

	const int32 VisualCount = FMath::Min(Count, 6);
	if (VisualCount <= 0) { return; }

	// Fan angles in upper semicircle
	// Left endpoint: stubs fan in upper-left semicircle (-170° to -60°)
	// Right endpoint: stubs fan in upper-right semicircle (-120° to -10°)
	const double StartAngleDeg = bLeftSide ? -170.0 : -120.0;
	const double EndAngleDeg = bLeftSide ? -60.0 : -10.0;

	const double StartAngle = FMath::DegreesToRadians(StartAngleDeg);
	const double EndAngle = FMath::DegreesToRadians(EndAngleDeg);
	const double AngleStep = (VisualCount > 1) ? (EndAngle - StartAngle) / (VisualCount - 1) : 0.0;

	for (int32 i = 0; i < VisualCount; ++i)
	{
		const double Angle = (VisualCount > 1) ? StartAngle + AngleStep * i : (StartAngle + EndAngle) * 0.5;
		const FVector2D StubEnd = Center + FVector2D(FMath::Cos(Angle) * StubLength, FMath::Sin(Angle) * StubLength);

		DrawEdgeLine(OutDrawElements, LayerId, AllottedGeometry, Center, StubEnd, NeighborStubColor, 1.0f);
		DrawFilledCircle(OutDrawElements, LayerId + 1, AllottedGeometry, StubEnd, StubDotRadius, NeighborStubColor);
	}
}

int32 SPCGExEdgeNeighborsCountPreview::PaintAttributeMode(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	int32 LayerId,
	const FVector2D& LocalSize) const
{
	using namespace PCGExEdgeFilterPreview;

	constexpr int32 NumPanels = 3;
	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - (NumPanels - 1) * PanelGap) / NumPanels;
	const float EdgeY = LocalSize.Y * 0.5f;
	const float EdgeMargin = 20.0f;

	for (int32 Panel = 0; Panel < NumPanels; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const FVector2D StartPos(PanelX + EdgeMargin, EdgeY);
		const FVector2D EndPos(PanelX + PanelWidth - EdgeMargin, EdgeY);

		DrawEdgeLine(OutDrawElements, LayerId, AllottedGeometry, StartPos, EndPos, AttributeModeColor, EdgeLineThickness);
		DrawFilledCircle(OutDrawElements, LayerId + 1, AllottedGeometry, StartPos, EndpointRadius, AttributeModeColor);
		DrawFilledCircle(OutDrawElements, LayerId + 1, AllottedGeometry, EndPos, EndpointRadius, AttributeModeColor);

		// Muted stubs
		const int32 StubCounts[NumPanels] = {3, 2, 4};
		DrawNeighborStubs(OutDrawElements, LayerId + 2, AllottedGeometry, StartPos, StubCounts[Panel], true);
		DrawNeighborStubs(OutDrawElements, LayerId + 2, AllottedGeometry, EndPos, StubCounts[Panel], false);
	}

	// "Per-Edge" label at center
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FVector2D TextPos(LocalSize.X * 0.5 - 22.0, LocalSize.Y * 0.5 - 6.0);
	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId + 4,
		AllottedGeometry.ToPaintGeometry(FVector2D(80, 16), FSlateLayoutTransform(TextPos)),
		TEXT("Per-Edge"), Font, ESlateDrawEffect::None, LabelColor);

	return LayerId + 5;
}

int32 SPCGExEdgeNeighborsCountPreview::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	using namespace PCGExEdgeFilterPreview;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	if (!bShowThreshold.Get())
	{
		return PaintAttributeMode(OutDrawElements, AllottedGeometry, LayerId, LocalSize);
	}

	const EPCGExRefineEdgeThresholdMode CurrentMode = Mode.Get();
	const EPCGExComparison CurrentComparison = Comparison.Get();
	const int32 T = FMath::Max(1, ThresholdConstant.Get());
	const int32 CurrentTolerance = Tolerance.Get();
	const bool bCurrentInvert = bInvert.Get();

	// 3 Panels with counts adapted to threshold T
	int32 FromCounts[3];
	int32 ToCounts[3];
	if (T <= 1)
	{
		FromCounts[0] = 1; ToCounts[0] = 1;
		FromCounts[1] = 2; ToCounts[1] = 1;
		FromCounts[2] = 3; ToCounts[2] = 1;
	}
	else
	{
		FromCounts[0] = T;                       ToCounts[0] = FMath::Max(1, T - 1);
		FromCounts[1] = FMath::Max(1, T + 1);   ToCounts[1] = 1;
		FromCounts[2] = FMath::Max(1, T - 1);   ToCounts[2] = FMath::Max(1, T - 1);
	}

	constexpr int32 NumPanels = 3;
	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - (NumPanels - 1) * PanelGap) / NumPanels;
	const float EdgeY = LocalSize.Y * 0.55f;
	const float EdgeMargin = 22.0f;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < NumPanels; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const int32 FC = FromCounts[Panel];
		const int32 TC = ToCounts[Panel];

		const bool bPass = Evaluate(CurrentMode, CurrentComparison, FC, TC, T, CurrentTolerance, bCurrentInvert);

		// Panel background
		const FLinearColor& BgColor = bPass ? PanelPassBg : PanelFailBg;
		DrawFilledRect(OutDrawElements, LayerId, AllottedGeometry,
		               FVector2D(PanelX, 0), FVector2D(PanelWidth, LocalSize.Y), BgColor);

		// Endpoint positions
		const FVector2D StartPos(PanelX + EdgeMargin, EdgeY);
		const FVector2D EndPos(PanelX + PanelWidth - EdgeMargin, EdgeY);

		// Draw neighbor stubs
		DrawNeighborStubs(OutDrawElements, LayerId + 1, AllottedGeometry, StartPos, FC, true);
		DrawNeighborStubs(OutDrawElements, LayerId + 1, AllottedGeometry, EndPos, TC, false);

		// Draw edge line
		DrawEdgeLine(OutDrawElements, LayerId + 3, AllottedGeometry, StartPos, EndPos, EdgeLineColor);

		// Draw endpoint circles
		DrawFilledCircle(OutDrawElements, LayerId + 4, AllottedGeometry, StartPos, EndpointRadius, EndpointNeutralColor);
		DrawFilledCircle(OutDrawElements, LayerId + 4, AllottedGeometry, EndPos, EndpointRadius, EndpointNeutralColor);

		// Count labels under each endpoint
		const FString FCStr = FString::Printf(TEXT("%d"), FC);
		const FVector2D FCPos(StartPos.X - 3.0, EdgeY + EndpointRadius + 3.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(16, 12), FSlateLayoutTransform(FCPos)),
			FCStr, Font, ESlateDrawEffect::None, LabelColor);

		const FString TCStr = FString::Printf(TEXT("%d"), TC);
		const FVector2D TCPos(EndPos.X - 3.0, EdgeY + EndpointRadius + 3.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(16, 12), FSlateLayoutTransform(TCPos)),
			TCStr, Font, ESlateDrawEffect::None, LabelColor);

		const float PanelCenterX = PanelX + PanelWidth * 0.5f;

		// Mode-specific indicators
		if (CurrentMode == EPCGExRefineEdgeThresholdMode::Sum)
		{
			// Sum value centered above edge
			const FString SumStr = FString::Printf(TEXT("Sum: %d"), FC + TC);
			const FVector2D SumPos(PanelCenterX - 16.0, EdgeY - 22.0);
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 5,
				AllottedGeometry.ToPaintGeometry(FVector2D(40, 12), FSlateLayoutTransform(SumPos)),
				SumStr, Font, ESlateDrawEffect::None, LabelColor);
		}
		else
		{
			// Per-endpoint comparison indicators (small checkmark/cross above each endpoint)
			const bool bFromPass = PCGExCompare::Compare(CurrentComparison, FC, T, static_cast<double>(CurrentTolerance));
			const bool bToPass = PCGExCompare::Compare(CurrentComparison, TC, T, static_cast<double>(CurrentTolerance));

			const FLinearColor FromIndColor = bFromPass ? EndpointPassColor : EndpointFailColor;
			const FLinearColor ToIndColor = bToPass ? EndpointPassColor : EndpointFailColor;

			// Small indicator circles above endpoints
			const FVector2D FromIndPos(StartPos.X, EdgeY - EndpointRadius - 10.0);
			DrawFilledCircle(OutDrawElements, LayerId + 5, AllottedGeometry, FromIndPos, 3.0, FromIndColor);

			const FVector2D ToIndPos(EndPos.X, EdgeY - EndpointRadius - 10.0);
			DrawFilledCircle(OutDrawElements, LayerId + 5, AllottedGeometry, ToIndPos, 3.0, ToIndColor);
		}

		// PASS/FAIL label
		const FString ResultStr = bPass ? TEXT("PASS") : TEXT("FAIL");
		const FLinearColor ResultColor = bPass ? EndpointPassColor : EndpointFailColor;
		const FVector2D ResultPos(PanelCenterX - 12.0, EdgeY + EndpointRadius + 18.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 12), FSlateLayoutTransform(ResultPos)),
			ResultStr, Font, ESlateDrawEffect::None, ResultColor);
	}

	// Top label: Mode name + "T: N"
	{
		FString TopLabel = FString::Printf(TEXT("%s | T: %d"), *GetModeName(CurrentMode), T);
		if (bCurrentInvert) { TopLabel += TEXT(" (inv)"); }
		const FVector2D LabelPos(LocalSize.X * 0.5 - 36.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 6,
			AllottedGeometry.ToPaintGeometry(FVector2D(100, 12), FSlateLayoutTransform(LabelPos)),
			TopLabel, Font, ESlateDrawEffect::None, LabelColor);
	}

	return LayerId + 7;
}
