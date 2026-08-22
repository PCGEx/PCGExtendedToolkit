// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchThumbnailRenderer.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchStyle.h"

namespace PCGExClusterSketchThumbnail
{
	constexpr float BaseThumbnailSize = 256.f;

	// The standalone editor's backdrop, so a thumbnail reads as a small view of that viewport.
	constexpr FLinearColor BackgroundColor = FLinearColor(0.016f, 0.018f, 0.022f);

	// All as a fraction of the thumbnail's smaller side, so every size holds at any zoom.
	constexpr float FramePadding = 0.08f;
	constexpr float VertexRadius = 0.030f;
	constexpr float EdgeThickness = 0.014f;

	constexpr int32 VertexSides = 12;
	constexpr float MinVertexRadius = 1.5f;
	constexpr float MinEdgeThickness = 1.0f;
}

bool UPCGExClusterSketchThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	// An empty sketch keeps the plain class icon rather than showing an empty frame.
	const UPCGExClusterSketch* Sketch = Cast<UPCGExClusterSketch>(Object);
	return Sketch && !Sketch->Model.Vertices.IsEmpty();
}

void UPCGExClusterSketchThumbnailRenderer::GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const
{
	OutWidth = FMath::TruncToInt(Zoom * PCGExClusterSketchThumbnail::BaseThumbnailSize);
	OutHeight = OutWidth;
}

void UPCGExClusterSketchThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	using namespace PCGExClusterSketchThumbnail;

	const UPCGExClusterSketch* Sketch = Cast<UPCGExClusterSketch>(Object);
	if (!Sketch || !Canvas || Width == 0 || Height == 0)
	{
		return;
	}

	{
		FCanvasTileItem Backdrop(FVector2D(X, Y), FVector2D(Width, Height), BackgroundColor);
		Backdrop.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Backdrop);
	}

	const FPCGExClusterSketchModel& Model = Sketch->Model;
	if (Model.Vertices.IsEmpty())
	{
		return;
	}

	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();

	FPCGExLatticeBasis Basis;
	const bool bHasBasis = Sketch->BuildBasis(Basis);
	const FPCGExLatticeBasis* BasisPtr = bHasBasis ? &Basis : nullptr;

	// Orthonormal view frame. Orthographic on purpose: perspective would make fit-to-frame depend on
	// distance, and a thumbnail has no reason to foreshorten.
	// The same angle the standalone editor opens on, so a thumbnail previews the view you get.
	const FVector Forward = Style->DefaultViewRotation.Vector().GetSafeNormal();
	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector; // straight-down view direction: any horizontal axis will do
	}
	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	// Project, tracking the extent as we go: the fit is derived from what is actually drawn, the same
	// rule the editor frames with (coord-derived where lattice-bound).
	TArray<FVector2D> Projected;
	Projected.SetNumUninitialized(Model.Vertices.Num());

	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());

	for (int32 i = 0; i < Model.Vertices.Num(); ++i)
	{
		const FVector Location = FPCGExClusterSketchModel::ResolvedLocation(Model.Vertices[i], BasisPtr);
		// Canvas Y grows downward, so world Up projects negative.
		const FVector2D P(FVector::DotProduct(Location, Right), -FVector::DotProduct(Location, Up));
		Projected[i] = P;
		Min.X = FMath::Min(Min.X, P.X);
		Min.Y = FMath::Min(Min.Y, P.Y);
		Max.X = FMath::Max(Max.X, P.X);
		Max.Y = FMath::Max(Max.Y, P.Y);
	}

	const float SmallSide = FMath::Min(static_cast<float>(Width), static_cast<float>(Height));
	const float Radius = FMath::Max(MinVertexRadius, SmallSide * VertexRadius);
	const float Thickness = FMath::Max(MinEdgeThickness, SmallSide * EdgeThickness);

	// Markers have screen size the projection knows nothing about, so the frame reserves room for them
	// on top of the padding -- otherwise a boundary vertex is drawn half outside the thumbnail.
	const float Inset = SmallSide * FramePadding + Radius;
	const float FitWidth = FMath::Max(1.f, Width - Inset * 2.f);
	const float FitHeight = FMath::Max(1.f, Height - Inset * 2.f);

	const FVector2D Extent = Max - Min;
	// A sketch collapsed to a line has no extent on one axis, and a single vertex has none at all:
	// fit on whichever axes exist, 1:1 when neither does, rather than dividing by zero.
	const bool bFitX = Extent.X > UE_DOUBLE_KINDA_SMALL_NUMBER;
	const bool bFitY = Extent.Y > UE_DOUBLE_KINDA_SMALL_NUMBER;
	double Scale = 1.0;
	if (bFitX && bFitY)
	{
		Scale = FMath::Min(FitWidth / Extent.X, FitHeight / Extent.Y);
	}
	else if (bFitX)
	{
		Scale = FitWidth / Extent.X;
	}
	else
		if (bFitY)
		{
			Scale = FitHeight / Extent.Y;
		}

	// Uniform scale about the projected centre, landing on the thumbnail's centre.
	const FVector2D SourceCentre = (Min + Max) * 0.5;
	const FVector2D TargetCentre(X + Width * 0.5f, Y + Height * 0.5f);

	for (FVector2D& P : Projected)
	{
		P = TargetCentre + (P - SourceCentre) * Scale;
	}

	// Edges first, so vertices cap their ends.
	for (const FPCGExClusterSketchEdge& E : Model.Edges)
	{
		// Dormant edges (an endpoint that does not exist yet) are an authoring state the sketch editor
		// surfaces and repairs; a thumbnail just shows the geometry that IS.
		if (!Projected.IsValidIndex(E.A) || !Projected.IsValidIndex(E.B))
		{
			continue;
		}
		FCanvasLineItem Line(Projected[E.A], Projected[E.B]);
		Line.LineThickness = Thickness;
		Line.SetColor(Style->EditEdge.Color);
		Canvas->DrawItem(Line);
	}

	for (int32 i = 0; i < Projected.Num(); ++i)
	{
		const FLinearColor Color = Model.Vertices[i].bLatticeBound ? Style->EditVertexBoundColor : Style->EditVertexIdle.Color;
		// Named, not a temporary: FCanvas::DrawItem takes a non-const reference.
		FCanvasNGonItem Marker(Projected[i], FVector2D(Radius), VertexSides, Color);
		Canvas->DrawItem(Marker);
	}
}
