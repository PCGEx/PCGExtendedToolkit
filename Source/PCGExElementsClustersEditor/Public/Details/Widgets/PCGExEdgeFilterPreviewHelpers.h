// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PCGExEdgeFilterPreview
{
	// Endpoint colors
	static const FLinearColor EndpointPassColor(0.1f, 0.6f, 0.2f, 0.85f);
	static const FLinearColor EndpointFailColor(0.6f, 0.15f, 0.15f, 0.7f);
	static const FLinearColor EndpointNeutralColor(0.7f, 0.7f, 0.7f, 0.8f);

	// Edge line
	static const FLinearColor EdgeLineColor(0.5f, 0.5f, 0.5f, 0.6f);

	// Panel backgrounds
	static const FLinearColor PanelPassBg(0.1f, 0.3f, 0.1f, 0.15f);
	static const FLinearColor PanelFailBg(0.3f, 0.1f, 0.1f, 0.15f);

	// Labels
	static const FLinearColor LabelColor(0.6f, 0.6f, 0.6f, 0.8f);
	static const FLinearColor AttributeModeColor(0.3f, 0.3f, 0.3f, 0.3f);

	// Neighbor stubs
	static const FLinearColor NeighborStubColor(0.4f, 0.5f, 0.6f, 0.5f);

	// Sizes
	static constexpr float EndpointRadius = 6.0f;
	static constexpr float EdgeLineThickness = 2.0f;
	static constexpr float StubLength = 12.0f;
	static constexpr float StubDotRadius = 2.5f;

	// Layout
	static constexpr float DesiredHeight = 140.0f;
	static constexpr float Padding = 6.0f;
	static constexpr float PanelGap = 6.0f;

	inline void DrawFilledCircle(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		const double Radius,
		const FLinearColor& Color,
		const int32 NumSegments = 16)
	{
		const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
		const FColor VertColor = Color.ToFColor(true);
		const FColor NoColor(0, 0, 0, 0);

		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(NumSegments + 2);
		Indices.Reserve(NumSegments * 3);

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

	inline void DrawFilledRect(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& TopLeft,
		const FVector2D& Size,
		const FLinearColor& Color)
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

	inline void DrawEdgeLine(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& From,
		const FVector2D& To,
		const FLinearColor& Color,
		const float Thickness = EdgeLineThickness)
	{
		TArray<FVector2D> LinePoints;
		LinePoints.Add(From);
		LinePoints.Add(To);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints, ESlateDrawEffect::None,
			Color, true, Thickness);
	}
}
