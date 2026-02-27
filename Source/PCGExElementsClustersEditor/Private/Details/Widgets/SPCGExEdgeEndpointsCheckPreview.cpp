// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Widgets/SPCGExEdgeEndpointsCheckPreview.h"
#include "Details/Widgets/PCGExEdgeFilterPreviewHelpers.h"

void SPCGExEdgeEndpointsCheckPreview::Construct(const FArguments& InArgs)
{
	Mode = InArgs._Mode;
	Expects = InArgs._Expects;
	bInvert = InArgs._bInvert;
}

FVector2D SPCGExEdgeEndpointsCheckPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(1.0f, PCGExEdgeFilterPreview::DesiredHeight);
}

bool SPCGExEdgeEndpointsCheckPreview::Evaluate(
	const EPCGExEdgeEndpointsCheckMode InMode,
	const EPCGExFilterResult InExpects,
	const bool bInInvert,
	const bool bStartRaw,
	const bool bEndRaw)
{
	const int8 Expected = (InExpects == EPCGExFilterResult::Fail) ? 0 : 1;
	const int8 S = bStartRaw ? 1 : 0;
	const int8 E = bEndRaw ? 1 : 0;

	bool bPass;
	switch (InMode)
	{
	case EPCGExEdgeEndpointsCheckMode::None:
		bPass = S != Expected && E != Expected;
		break;
	case EPCGExEdgeEndpointsCheckMode::Both:
		bPass = S == Expected && E == Expected;
		break;
	case EPCGExEdgeEndpointsCheckMode::Any:
		bPass = S == Expected || E == Expected;
		break;
	case EPCGExEdgeEndpointsCheckMode::Start:
		bPass = S == Expected;
		break;
	case EPCGExEdgeEndpointsCheckMode::End:
		bPass = E == Expected;
		break;
	case EPCGExEdgeEndpointsCheckMode::SeeSaw:
		bPass = S != E;
		break;
	default:
		bPass = false;
		break;
	}

	return bInInvert ? !bPass : bPass;
}

FString SPCGExEdgeEndpointsCheckPreview::GetModeName(const EPCGExEdgeEndpointsCheckMode InMode)
{
	switch (InMode)
	{
	case EPCGExEdgeEndpointsCheckMode::None: return TEXT("None");
	case EPCGExEdgeEndpointsCheckMode::Both: return TEXT("Both");
	case EPCGExEdgeEndpointsCheckMode::Any: return TEXT("Any");
	case EPCGExEdgeEndpointsCheckMode::Start: return TEXT("Start");
	case EPCGExEdgeEndpointsCheckMode::End: return TEXT("End");
	case EPCGExEdgeEndpointsCheckMode::SeeSaw: return TEXT("SeeSaw");
	default: return TEXT("?");
	}
}

int32 SPCGExEdgeEndpointsCheckPreview::OnPaint(
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
	const EPCGExEdgeEndpointsCheckMode CurrentMode = Mode.Get();
	const EPCGExFilterResult CurrentExpects = Expects.Get();
	const bool bCurrentInvert = bInvert.Get();

	// 4 panels: (Pass,Pass), (Pass,Fail), (Fail,Pass), (Fail,Fail)
	constexpr int32 NumPanels = 4;
	const bool PanelInputs[NumPanels][2] = {{true, true}, {true, false}, {false, true}, {false, false}};

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - (NumPanels - 1) * PanelGap) / NumPanels;
	const float EdgeY = LocalSize.Y * 0.5f;
	const float EdgeMargin = 16.0f;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < NumPanels; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const bool bStartRaw = PanelInputs[Panel][0];
		const bool bEndRaw = PanelInputs[Panel][1];

		const bool bOverallPass = Evaluate(CurrentMode, CurrentExpects, bCurrentInvert, bStartRaw, bEndRaw);

		// Panel background
		const FLinearColor& BgColor = bOverallPass ? PanelPassBg : PanelFailBg;
		DrawFilledRect(OutDrawElements, LayerId, AllottedGeometry,
		               FVector2D(PanelX, 0), FVector2D(PanelWidth, LocalSize.Y), BgColor);

		// Edge line endpoints
		const FVector2D StartPos(PanelX + EdgeMargin, EdgeY);
		const FVector2D EndPos(PanelX + PanelWidth - EdgeMargin, EdgeY);

		// Draw edge line
		DrawEdgeLine(OutDrawElements, LayerId + 1, AllottedGeometry, StartPos, EndPos, EdgeLineColor);

		// Draw endpoint circles
		const FLinearColor& StartColor = bStartRaw ? EndpointPassColor : EndpointFailColor;
		const FLinearColor& EndColor = bEndRaw ? EndpointPassColor : EndpointFailColor;
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, StartPos, EndpointRadius, StartColor);
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, EndPos, EndpointRadius, EndColor);

		// S / E labels under endpoints
		const FVector2D SLabelPos(StartPos.X - 3.0, EdgeY + EndpointRadius + 3.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(10, 12), FSlateLayoutTransform(SLabelPos)),
			TEXT("S"), Font, ESlateDrawEffect::None, LabelColor);

		const FVector2D ELabelPos(EndPos.X - 3.0, EdgeY + EndpointRadius + 3.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(10, 12), FSlateLayoutTransform(ELabelPos)),
			TEXT("E"), Font, ESlateDrawEffect::None, LabelColor);

		// PASS/FAIL result label below
		const FString ResultStr = bOverallPass ? TEXT("PASS") : TEXT("FAIL");
		const FLinearColor ResultColor = bOverallPass ? EndpointPassColor : EndpointFailColor;
		const float PanelCenterX = PanelX + PanelWidth * 0.5f;
		const FVector2D ResultPos(PanelCenterX - 12.0, EdgeY + EndpointRadius + 18.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 12), FSlateLayoutTransform(ResultPos)),
			ResultStr, Font, ESlateDrawEffect::None, ResultColor);
	}

	// Top label: Mode name
	{
		FString TopLabel = GetModeName(CurrentMode);
		if (bCurrentInvert) { TopLabel += TEXT(" (inv)"); }
		const FVector2D LabelPos(LocalSize.X * 0.5 - 30.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 12), FSlateLayoutTransform(LabelPos)),
			TopLabel, Font, ESlateDrawEffect::None, LabelColor);
	}

	return LayerId + 5;
}
