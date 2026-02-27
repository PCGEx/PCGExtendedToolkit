// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Widgets/SPCGExEdgeEndpointsCompareNumPreview.h"
#include "Details/Widgets/PCGExEdgeFilterPreviewHelpers.h"

void SPCGExEdgeEndpointsCompareNumPreview::Construct(const FArguments& InArgs)
{
	Comparison = InArgs._Comparison;
	bInvert = InArgs._bInvert;
}

FVector2D SPCGExEdgeEndpointsCompareNumPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(1.0f, PCGExEdgeFilterPreview::DesiredHeight);
}

int32 SPCGExEdgeEndpointsCompareNumPreview::OnPaint(
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
	const EPCGExComparison CurrentComparison = Comparison.Get();
	const bool bCurrentInvert = bInvert.Get();

	// 3 Panels: Start>End, Start==End, Start<End
	constexpr int32 NumPanels = 3;
	const double StartVals[NumPanels] = {7.0, 5.0, 3.0};
	const double EndVals[NumPanels] = {3.0, 5.0, 7.0};

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - (NumPanels - 1) * PanelGap) / NumPanels;
	const float EdgeY = LocalSize.Y * 0.62f;
	const float EdgeMargin = 20.0f;
	const float MaxBarHeight = 35.0f;
	const float MaxValue = 7.0f;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < NumPanels; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const double SVal = StartVals[Panel];
		const double EVal = EndVals[Panel];

		// Evaluate
		bool bPass = PCGExCompare::Compare(CurrentComparison, SVal, EVal, DBL_COMPARE_TOLERANCE);
		if (bCurrentInvert) { bPass = !bPass; }

		// Panel background
		const FLinearColor& BgColor = bPass ? PanelPassBg : PanelFailBg;
		DrawFilledRect(OutDrawElements, LayerId, AllottedGeometry,
		               FVector2D(PanelX, 0), FVector2D(PanelWidth, LocalSize.Y), BgColor);

		// Endpoint positions
		const FVector2D StartPos(PanelX + EdgeMargin, EdgeY);
		const FVector2D EndPos(PanelX + PanelWidth - EdgeMargin, EdgeY);

		// Draw edge line
		DrawEdgeLine(OutDrawElements, LayerId + 1, AllottedGeometry, StartPos, EndPos, EdgeLineColor);

		// Draw value bars above endpoints
		const float StartBarH = static_cast<float>(SVal / MaxValue) * MaxBarHeight;
		const float EndBarH = static_cast<float>(EVal / MaxValue) * MaxBarHeight;
		const float BarWidth = 6.0f;

		const FLinearColor BarColor = EndpointNeutralColor;
		DrawFilledRect(OutDrawElements, LayerId + 1, AllottedGeometry,
		               FVector2D(StartPos.X - BarWidth * 0.5, EdgeY - EndpointRadius - StartBarH),
		               FVector2D(BarWidth, StartBarH), BarColor);
		DrawFilledRect(OutDrawElements, LayerId + 1, AllottedGeometry,
		               FVector2D(EndPos.X - BarWidth * 0.5, EdgeY - EndpointRadius - EndBarH),
		               FVector2D(BarWidth, EndBarH), BarColor);

		// Draw endpoint circles at bar base
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, StartPos, EndpointRadius, EndpointNeutralColor);
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, EndPos, EndpointRadius, EndpointNeutralColor);

		// Value labels above bars
		const FString SValStr = FString::Printf(TEXT("%d"), static_cast<int32>(SVal));
		const FVector2D SValPos(StartPos.X - 3.0, EdgeY - EndpointRadius - StartBarH - 14.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(16, 12), FSlateLayoutTransform(SValPos)),
			SValStr, Font, ESlateDrawEffect::None, LabelColor);

		const FString EValStr = FString::Printf(TEXT("%d"), static_cast<int32>(EVal));
		const FVector2D EValPos(EndPos.X - 3.0, EdgeY - EndpointRadius - EndBarH - 14.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(16, 12), FSlateLayoutTransform(EValPos)),
			EValStr, Font, ESlateDrawEffect::None, LabelColor);

		// Comparison operator symbol centered between endpoints
		const FString OpStr = PCGExCompare::ToString(CurrentComparison);
		const float MidX = (StartPos.X + EndPos.X) * 0.5f;
		const FVector2D OpPos(MidX - 8.0, EdgeY - 18.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(30, 12), FSlateLayoutTransform(OpPos)),
			OpStr, Font, ESlateDrawEffect::None, LabelColor);

		// PASS/FAIL label below edge
		const FString ResultStr = bPass ? TEXT("PASS") : TEXT("FAIL");
		const FLinearColor ResultColor = bPass ? EndpointPassColor : EndpointFailColor;
		const float PanelCenterX = PanelX + PanelWidth * 0.5f;
		const FVector2D ResultPos(PanelCenterX - 12.0, EdgeY + EndpointRadius + 8.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 12), FSlateLayoutTransform(ResultPos)),
			ResultStr, Font, ESlateDrawEffect::None, ResultColor);
	}

	// Top label
	{
		FString TopLabel = TEXT("Start vs End");
		if (bCurrentInvert) { TopLabel += TEXT(" (inv)"); }
		const FVector2D LabelPos(LocalSize.X * 0.5 - 30.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 12), FSlateLayoutTransform(LabelPos)),
			TopLabel, Font, ESlateDrawEffect::None, LabelColor);
	}

	return LayerId + 5;
}
