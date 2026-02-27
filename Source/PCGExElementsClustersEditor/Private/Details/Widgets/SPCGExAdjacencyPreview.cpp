// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Widgets/SPCGExAdjacencyPreview.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PCGExAdjacencyPreviewConstants
{
	// Branch colors
	static const FLinearColor PassBranchColor(0.1f, 0.6f, 0.2f, 0.8f);
	static const FLinearColor FailBranchColor(0.4f, 0.15f, 0.15f, 0.5f);

	// Node colors
	static const FLinearColor CentralNodeColor(0.9f, 0.9f, 0.9f, 1.0f);

	// Panel background tints
	static const FLinearColor PanelPassBg(0.1f, 0.3f, 0.1f, 0.15f);
	static const FLinearColor PanelFailBg(0.3f, 0.1f, 0.1f, 0.15f);

	// Aggregated mode colors
	static const FLinearColor AggregatedBranchColor(0.5f, 0.6f, 0.7f, 0.7f);
	static const FLinearColor AggregatedHighlightColor(0.9f, 0.85f, 0.3f, 0.9f);

	// Labels
	static const FLinearColor LabelColor(0.6f, 0.6f, 0.6f, 0.8f);
	static const FLinearColor AttributeModeColor(0.3f, 0.3f, 0.3f, 0.3f);

	static constexpr float BranchThickness = 1.5f;
	static constexpr float NeighborDotRadius = 3.0f;
	static constexpr float CentralDotRadius = 4.0f;
}

void SPCGExAdjacencyPreview::Construct(const FArguments& InArgs)
{
	Mode = InArgs._Mode;
	Consolidation = InArgs._Consolidation;
	ThresholdComparison = InArgs._ThresholdComparison;
	ThresholdType = InArgs._ThresholdType;
	DiscreteThreshold = InArgs._DiscreteThreshold;
	RelativeThreshold = InArgs._RelativeThreshold;
	Rounding = InArgs._Rounding;
	ThresholdTolerance = InArgs._ThresholdTolerance;
	bShowThreshold = InArgs._bShowThreshold;
}

FVector2D SPCGExAdjacencyPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(1.0f, DesiredHeight); // Stretch to fill available width
}

int32 SPCGExAdjacencyPreview::ComputeThreshold(const int32 TotalNeighbors) const
{
	const EPCGExMeanMeasure CurrentThresholdType = ThresholdType.Get();
	if (CurrentThresholdType == EPCGExMeanMeasure::Discrete)
	{
		return DiscreteThreshold.Get();
	}

	// Relative mode
	const double Fraction = RelativeThreshold.Get();
	const double Raw = Fraction * TotalNeighbors;
	const EPCGExRelativeThresholdRoundingMode RoundMode = Rounding.Get();

	switch (RoundMode)
	{
	case EPCGExRelativeThresholdRoundingMode::Floor: return FMath::FloorToInt32(Raw);
	case EPCGExRelativeThresholdRoundingMode::Ceil: return FMath::CeilToInt32(Raw);
	default: return FMath::RoundToInt32(Raw);
	}
}

void SPCGExAdjacencyPreview::DrawFilledCircle(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const double Radius,
	const FLinearColor& Color,
	const int32 NumSegments) const
{
	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor VertColor = Color.ToFColor(true);
	const FColor NoColor(0, 0, 0, 0);

	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve(NumSegments + 2);
	Indices.Reserve(NumSegments * 3);

	// Center vertex
	Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Center), FVector2f::ZeroVector, VertColor, NoColor));

	const double AngleStep = UE_TWO_PI / NumSegments;
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const double Angle = AngleStep * i;
		const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Pos), FVector2f::ZeroVector, VertColor, NoColor));
	}

	for (int32 i = 0; i < NumSegments; ++i)
	{
		Indices.Add(0);
		Indices.Add(i + 1);
		Indices.Add(i + 2);
	}

	const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
		*FCoreStyle::Get().GetDefaultBrush());

	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, Vertices, Indices, nullptr, 0, 0);
}

void SPCGExAdjacencyPreview::DrawFilledRect(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& TopLeft,
	const FVector2D& Size,
	const FLinearColor& Color) const
{
	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor VertColor = Color.ToFColor(true);
	const FColor NoColor(0, 0, 0, 0);

	const FVector2D TR = TopLeft + FVector2D(Size.X, 0);
	const FVector2D BL = TopLeft + FVector2D(0, Size.Y);
	const FVector2D BR = TopLeft + Size;

	TArray<FSlateVertex> Vertices;
	Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(TopLeft), FVector2f::ZeroVector, VertColor, NoColor));
	Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(TR), FVector2f::ZeroVector, VertColor, NoColor));
	Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(BR), FVector2f::ZeroVector, VertColor, NoColor));
	Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(BL), FVector2f::ZeroVector, VertColor, NoColor));

	TArray<SlateIndex> Indices = {0, 1, 2, 0, 2, 3};

	const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
		*FCoreStyle::Get().GetDefaultBrush());

	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, Vertices, Indices, nullptr, 0, 0);
}

void SPCGExAdjacencyPreview::DrawStarPanel(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	const int32 LayerId,
	const FVector2D& Center,
	const double Radius,
	const int32 TotalBranches,
	const int32 GreenCount,
	const bool bOverallPass) const
{
	if (TotalBranches <= 0) { return; }

	const double AngleStep = UE_TWO_PI / TotalBranches;
	// Start from top (-PI/2), green branches first
	const double StartAngle = -UE_HALF_PI;

	for (int32 i = 0; i < TotalBranches; ++i)
	{
		const double Angle = StartAngle + AngleStep * i;
		const bool bIsGreen = i < GreenCount;
		const FLinearColor& BranchColor = bIsGreen
			                                  ? PCGExAdjacencyPreviewConstants::PassBranchColor
			                                  : PCGExAdjacencyPreviewConstants::FailBranchColor;

		const FVector2D EndPoint = Center + FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);

		// Draw branch line
		TArray<FVector2D> LinePoints;
		LinePoints.Add(Center);
		LinePoints.Add(EndPoint);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints, ESlateDrawEffect::None,
			BranchColor, true,
			PCGExAdjacencyPreviewConstants::BranchThickness);

		// Draw neighbor dot
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, EndPoint,
		                 PCGExAdjacencyPreviewConstants::NeighborDotRadius, BranchColor);
	}

	// Draw central node
	DrawFilledCircle(OutDrawElements, LayerId + 3, AllottedGeometry, Center,
	                 PCGExAdjacencyPreviewConstants::CentralDotRadius, PCGExAdjacencyPreviewConstants::CentralNodeColor);
}

void SPCGExAdjacencyPreview::DrawAggregatedPanel(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	const int32 LayerId,
	const FVector2D& Center,
	const double MaxRadius,
	const TArray<double>& Values,
	const int32 HighlightIndex,
	const EPCGExAdjacencyGatherMode GatherMode) const
{
	const int32 N = Values.Num();
	if (N <= 0) { return; }

	const double AngleStep = UE_TWO_PI / N;
	const double StartAngle = -UE_HALF_PI;
	const double MinRadius = MaxRadius * 0.15; // Minimum branch length for visibility

	for (int32 i = 0; i < N; ++i)
	{
		const double Angle = StartAngle + AngleStep * i;
		const double BranchRadius = MinRadius + Values[i] * (MaxRadius - MinRadius);
		const bool bHighlight = (HighlightIndex < 0) || (i == HighlightIndex);
		const FLinearColor& BranchColor = bHighlight
			                                  ? PCGExAdjacencyPreviewConstants::AggregatedHighlightColor
			                                  : PCGExAdjacencyPreviewConstants::AggregatedBranchColor;

		const FVector2D EndPoint = Center + FVector2D(FMath::Cos(Angle) * BranchRadius, FMath::Sin(Angle) * BranchRadius);

		// Draw branch line
		TArray<FVector2D> LinePoints;
		LinePoints.Add(Center);
		LinePoints.Add(EndPoint);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints, ESlateDrawEffect::None,
			BranchColor, true,
			PCGExAdjacencyPreviewConstants::BranchThickness);

		// Draw endpoint dot
		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, EndPoint,
		                 PCGExAdjacencyPreviewConstants::NeighborDotRadius, BranchColor);
	}

	// Draw central node
	DrawFilledCircle(OutDrawElements, LayerId + 3, AllottedGeometry, Center,
	                 PCGExAdjacencyPreviewConstants::CentralDotRadius, PCGExAdjacencyPreviewConstants::CentralNodeColor);
}

int32 SPCGExAdjacencyPreview::PaintSomeMode(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	int32 LayerId,
	const FVector2D& LocalSize,
	const int32 Threshold,
	const EPCGExComparison Comparison,
	const int32 Tolerance) const
{
	const int32 TotalNeighbors = FMath::Max(Threshold + 2, 5);

	// Compute three green-count values
	int32 Counts[3];
	if (Threshold <= 0)
	{
		Counts[0] = 0;
		Counts[1] = 1;
		Counts[2] = 2;
	}
	else if (Threshold >= TotalNeighbors - 1)
	{
		Counts[0] = TotalNeighbors - 2;
		Counts[1] = TotalNeighbors - 1;
		Counts[2] = TotalNeighbors;
	}
	else
	{
		Counts[0] = Threshold - 1;
		Counts[1] = Threshold;
		Counts[2] = Threshold + 1;
	}

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - 2.0f * PanelGap) / 3.0f;
	const float StarCenterY = LocalSize.Y * 0.45f;
	const double StarRadius = FMath::Min(PanelWidth * 0.5 - 8.0, static_cast<double>(MaxStarRadius));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < 3; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const FVector2D PanelCenter(PanelX + PanelWidth * 0.5f, StarCenterY);
		const int32 GreenCount = FMath::Clamp(Counts[Panel], 0, TotalNeighbors);

		// Evaluate pass/fail
		const bool bPass = PCGExCompare::Compare(Comparison, GreenCount, Threshold, static_cast<double>(Tolerance));

		// Draw panel background
		const FLinearColor& BgColor = bPass
			                              ? PCGExAdjacencyPreviewConstants::PanelPassBg
			                              : PCGExAdjacencyPreviewConstants::PanelFailBg;
		DrawFilledRect(OutDrawElements, LayerId, AllottedGeometry,
		               FVector2D(PanelX, 0), FVector2D(PanelWidth, LocalSize.Y), BgColor);

		// Draw star
		DrawStarPanel(OutDrawElements, AllottedGeometry, LayerId, PanelCenter, StarRadius,
		              TotalNeighbors, GreenCount, bPass);

		// Count label below star
		const FString CountStr = FString::Printf(TEXT("%d/%d"), GreenCount, TotalNeighbors);
		const FVector2D LabelPos(PanelCenter.X - 14.0, StarCenterY + StarRadius + 6.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 14), FSlateLayoutTransform(LabelPos)),
			CountStr, Font, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::LabelColor);

		// Pass/fail label
		const FString ResultStr = bPass ? TEXT("PASS") : TEXT("FAIL");
		const FLinearColor ResultColor = bPass
			                                 ? PCGExAdjacencyPreviewConstants::PassBranchColor
			                                 : PCGExAdjacencyPreviewConstants::FailBranchColor;
		const FVector2D ResultPos(PanelCenter.X - 12.0, StarCenterY + StarRadius + 18.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 14), FSlateLayoutTransform(ResultPos)),
			ResultStr, Font, ESlateDrawEffect::None,
			ResultColor);
	}

	// Threshold label at top
	{
		const FString ThresholdStr = FString::Printf(TEXT("Threshold: %d"), Threshold);
		const FVector2D ThreshPos(LocalSize.X * 0.5 - 30.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 12), FSlateLayoutTransform(ThreshPos)),
			ThresholdStr, Font, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::LabelColor);
	}

	return LayerId + 6;
}

int32 SPCGExAdjacencyPreview::PaintAllIndividualMode(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	int32 LayerId,
	const FVector2D& LocalSize) const
{
	constexpr int32 TotalBranches = 5;
	const int32 GreenCounts[3] = {5, 4, 1};

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - 2.0f * PanelGap) / 3.0f;
	const float StarCenterY = LocalSize.Y * 0.45f;
	const double StarRadius = FMath::Min(PanelWidth * 0.5 - 8.0, static_cast<double>(MaxStarRadius));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < 3; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const FVector2D PanelCenter(PanelX + PanelWidth * 0.5f, StarCenterY);
		const int32 GreenCount = GreenCounts[Panel];
		const bool bPass = (GreenCount == TotalBranches);

		// Panel background
		const FLinearColor& BgColor = bPass
			                              ? PCGExAdjacencyPreviewConstants::PanelPassBg
			                              : PCGExAdjacencyPreviewConstants::PanelFailBg;
		DrawFilledRect(OutDrawElements, LayerId, AllottedGeometry,
		               FVector2D(PanelX, 0), FVector2D(PanelWidth, LocalSize.Y), BgColor);

		// Star
		DrawStarPanel(OutDrawElements, AllottedGeometry, LayerId, PanelCenter, StarRadius,
		              TotalBranches, GreenCount, bPass);

		// Count label
		const FString CountStr = FString::Printf(TEXT("%d/%d"), GreenCount, TotalBranches);
		const FVector2D LabelPos(PanelCenter.X - 14.0, StarCenterY + StarRadius + 6.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 14), FSlateLayoutTransform(LabelPos)),
			CountStr, Font, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::LabelColor);

		// Pass/fail
		const FString ResultStr = bPass ? TEXT("PASS") : TEXT("FAIL");
		const FLinearColor ResultColor = bPass
			                                 ? PCGExAdjacencyPreviewConstants::PassBranchColor
			                                 : PCGExAdjacencyPreviewConstants::FailBranchColor;
		const FVector2D ResultPos(PanelCenter.X - 12.0, StarCenterY + StarRadius + 18.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 14), FSlateLayoutTransform(ResultPos)),
			ResultStr, Font, ESlateDrawEffect::None,
			ResultColor);
	}

	// Mode label
	{
		const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
		const FVector2D LabelPos(LocalSize.X * 0.5 - 30.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 12), FSlateLayoutTransform(LabelPos)),
			TEXT("All : Individual"), SmallFont, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::LabelColor);
	}

	return LayerId + 6;
}

int32 SPCGExAdjacencyPreview::PaintAllAggregatedMode(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	int32 LayerId,
	const FVector2D& LocalSize,
	const EPCGExAdjacencyGatherMode GatherMode) const
{
	constexpr int32 N = 5;

	// Three different value distributions
	const TArray<double> ValuesA = {0.8, 0.7, 0.6, 0.5, 0.9};
	const TArray<double> ValuesB = {0.9, 0.8, 0.2, 0.7, 0.6};
	const TArray<double> ValuesC = {0.2, 0.3, 0.1, 0.9, 0.8};
	const TArray<double>* AllValues[3] = {&ValuesA, &ValuesB, &ValuesC};

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - 2.0f * PanelGap) / 3.0f;
	const float StarCenterY = LocalSize.Y * 0.45f;
	const double StarRadius = FMath::Min(PanelWidth * 0.5 - 8.0, static_cast<double>(MaxStarRadius));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Panel = 0; Panel < 3; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const FVector2D PanelCenter(PanelX + PanelWidth * 0.5f, StarCenterY);
		const TArray<double>& Values = *AllValues[Panel];

		// Compute aggregate and highlight index
		int32 HighlightIndex = -1; // -1 = highlight all
		double Aggregate = 0.0;
		FString AggLabel;

		switch (GatherMode)
		{
		case EPCGExAdjacencyGatherMode::Average:
			{
				double Sum = 0.0;
				for (const double V : Values) { Sum += V; }
				Aggregate = Sum / N;
				AggLabel = FString::Printf(TEXT("avg:%.2f"), Aggregate);
				HighlightIndex = -1; // All highlighted
				break;
			}
		case EPCGExAdjacencyGatherMode::Min:
			{
				double MinVal = Values[0];
				HighlightIndex = 0;
				for (int32 i = 1; i < N; ++i)
				{
					if (Values[i] < MinVal)
					{
						MinVal = Values[i];
						HighlightIndex = i;
					}
				}
				Aggregate = MinVal;
				AggLabel = FString::Printf(TEXT("min:%.2f"), Aggregate);
				break;
			}
		case EPCGExAdjacencyGatherMode::Max:
			{
				double MaxVal = Values[0];
				HighlightIndex = 0;
				for (int32 i = 1; i < N; ++i)
				{
					if (Values[i] > MaxVal)
					{
						MaxVal = Values[i];
						HighlightIndex = i;
					}
				}
				Aggregate = MaxVal;
				AggLabel = FString::Printf(TEXT("max:%.2f"), Aggregate);
				break;
			}
		case EPCGExAdjacencyGatherMode::Sum:
			{
				double Sum = 0.0;
				for (const double V : Values) { Sum += V; }
				Aggregate = Sum;
				AggLabel = FString::Printf(TEXT("sum:%.1f"), Aggregate);
				HighlightIndex = -1; // All highlighted
				break;
			}
		default: break;
		}

		// Draw star
		DrawAggregatedPanel(OutDrawElements, AllottedGeometry, LayerId, PanelCenter, StarRadius,
		                    Values, HighlightIndex, GatherMode);

		// Aggregate label below star
		const FVector2D LabelPos(PanelCenter.X - 20.0, StarCenterY + StarRadius + 6.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(50, 14), FSlateLayoutTransform(LabelPos)),
			AggLabel, Font, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::AggregatedHighlightColor);
	}

	// Mode label at top
	{
		FString ModeStr;
		switch (GatherMode)
		{
		case EPCGExAdjacencyGatherMode::Average: ModeStr = TEXT("All : Average"); break;
		case EPCGExAdjacencyGatherMode::Min: ModeStr = TEXT("All : Min"); break;
		case EPCGExAdjacencyGatherMode::Max: ModeStr = TEXT("All : Max"); break;
		case EPCGExAdjacencyGatherMode::Sum: ModeStr = TEXT("All : Sum"); break;
		default: ModeStr = TEXT("All : Aggregated"); break;
		}

		const FVector2D LabelPos(LocalSize.X * 0.5 - 30.0, 2.0);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 5,
			AllottedGeometry.ToPaintGeometry(FVector2D(80, 12), FSlateLayoutTransform(LabelPos)),
			ModeStr, Font, ESlateDrawEffect::None,
			PCGExAdjacencyPreviewConstants::LabelColor);
	}

	return LayerId + 6;
}

int32 SPCGExAdjacencyPreview::PaintAttributeMode(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	int32 LayerId,
	const FVector2D& LocalSize) const
{
	constexpr int32 TotalBranches = 5;

	const float ContentWidth = LocalSize.X - 2.0f * Padding;
	const float PanelWidth = (ContentWidth - 2.0f * PanelGap) / 3.0f;
	const float StarCenterY = LocalSize.Y * 0.45f;
	const double StarRadius = FMath::Min(PanelWidth * 0.5 - 8.0, static_cast<double>(MaxStarRadius));

	for (int32 Panel = 0; Panel < 3; ++Panel)
	{
		const float PanelX = Padding + Panel * (PanelWidth + PanelGap);
		const FVector2D PanelCenter(PanelX + PanelWidth * 0.5f, StarCenterY);

		const double AngleStep = UE_TWO_PI / TotalBranches;
		const double StartAngle = -UE_HALF_PI;

		for (int32 i = 0; i < TotalBranches; ++i)
		{
			const double Angle = StartAngle + AngleStep * i;
			const FVector2D EndPoint = PanelCenter + FVector2D(FMath::Cos(Angle) * StarRadius, FMath::Sin(Angle) * StarRadius);

			TArray<FVector2D> LinePoints;
			LinePoints.Add(PanelCenter);
			LinePoints.Add(EndPoint);
			FSlateDrawElement::MakeLines(
				OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints, ESlateDrawEffect::None,
				PCGExAdjacencyPreviewConstants::AttributeModeColor, true,
				PCGExAdjacencyPreviewConstants::BranchThickness);

			DrawFilledCircle(OutDrawElements, LayerId + 1, AllottedGeometry, EndPoint,
			                 PCGExAdjacencyPreviewConstants::NeighborDotRadius,
			                 PCGExAdjacencyPreviewConstants::AttributeModeColor);
		}

		DrawFilledCircle(OutDrawElements, LayerId + 2, AllottedGeometry, PanelCenter,
		                 PCGExAdjacencyPreviewConstants::CentralDotRadius,
		                 PCGExAdjacencyPreviewConstants::AttributeModeColor);
	}

	// "Per-Point" label at center
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FVector2D TextPos(LocalSize.X * 0.5 - 22.0, LocalSize.Y * 0.5 - 6.0);
	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId + 4,
		AllottedGeometry.ToPaintGeometry(FVector2D(80, 16), FSlateLayoutTransform(TextPos)),
		TEXT("Per-Point"), Font, ESlateDrawEffect::None,
		PCGExAdjacencyPreviewConstants::LabelColor);

	return LayerId + 5;
}

int32 SPCGExAdjacencyPreview::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	// Check attribute mode first
	if (!bShowThreshold.Get())
	{
		return PaintAttributeMode(OutDrawElements, AllottedGeometry, LayerId, LocalSize);
	}

	const EPCGExAdjacencyTestMode CurrentMode = Mode.Get();

	if (CurrentMode == EPCGExAdjacencyTestMode::Some)
	{
		// "Some" mode — threshold counting
		const EPCGExMeanMeasure CurrentThresholdType = ThresholdType.Get();
		int32 TotalForCalc;
		if (CurrentThresholdType == EPCGExMeanMeasure::Relative)
		{
			TotalForCalc = 6; // Fixed for relative mode visualization
		}
		else
		{
			TotalForCalc = FMath::Max(DiscreteThreshold.Get() + 2, 5);
		}

		const int32 Threshold = ComputeThreshold(TotalForCalc);
		const EPCGExComparison Comparison = ThresholdComparison.Get();
		const int32 Tolerance = ThresholdTolerance.Get();

		return PaintSomeMode(OutDrawElements, AllottedGeometry, LayerId, LocalSize, Threshold, Comparison, Tolerance);
	}

	// "All" mode
	const EPCGExAdjacencyGatherMode CurrentConsolidation = Consolidation.Get();

	if (CurrentConsolidation == EPCGExAdjacencyGatherMode::Individual)
	{
		return PaintAllIndividualMode(OutDrawElements, AllottedGeometry, LayerId, LocalSize);
	}

	return PaintAllAggregatedMode(OutDrawElements, AllottedGeometry, LayerId, LocalSize, CurrentConsolidation);
}
