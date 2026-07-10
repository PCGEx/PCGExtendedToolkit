// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#include "SPCGExPropertyCurveGraph.h"

#include "PCGExPropertiesEditorStyle.h"
#include "PCGExPropertyCurveEditController.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PCGExPropertyCurveGraphColors
{
	static constexpr FLinearColor Background(0.0f, 0.0f, 0.0f, 0.25f);
	static constexpr FLinearColor Border(1.0f, 1.0f, 1.0f, 0.15f);
	static constexpr FLinearColor Grid(1.0f, 1.0f, 1.0f, 0.06f);
	static constexpr FLinearColor Curve(0.9f, 0.9f, 0.95f, 1.0f);
	static constexpr FLinearColor KeyNormal(0.8f, 0.8f, 0.85f, 1.0f);
	static constexpr FLinearColor KeySelected(1.0f, 0.6f, 0.1f, 1.0f);
	// Signed-area fill: the band between the curve and the value=0 baseline, so positive vs negative
	// regions read at a glance.
	static constexpr FLinearColor Fill(0.9f, 0.9f, 0.95f, 0.07f);
	// Vertical drop-lines linking a graph key to its strip gem: faint for unselected, the selection
	// colour (solid) for the selected key.
	static constexpr FLinearColor ConnectorNormal(1.0f, 1.0f, 1.0f, 0.12f);
	static constexpr FLinearColor ConnectorSelected(1.0f, 0.6f, 0.1f, 0.6f);
}

void SPCGExPropertyCurveGraph::Construct(const FArguments& InArgs, const TSharedRef<FPCGExPropertyCurveEditController>& InController)
{
	Init(InController);
}

FVector2D SPCGExPropertyCurveGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}

#pragma region Coordinate mapping

float SPCGExPropertyCurveGraph::ValueToLocalY(float Value, const FVector2D& Size) const
{
	const float Top = Padding;
	const float Bottom = Size.Y - Padding;
	const float Alpha = Controller->ValueToFrameAlpha(Value);
	return Bottom - Alpha * FMath::Max(Bottom - Top, 1.0f);
}

float SPCGExPropertyCurveGraph::LocalYToValue(float LocalY, const FVector2D& Size) const
{
	const float Top = Padding;
	const float Bottom = Size.Y - Padding;
	const float Alpha = (Bottom - LocalY) / FMath::Max(Bottom - Top, 1.0f);
	// No clamp: the value follows the cursor unbounded. The frame is frozen mid-drag (stable mapping,
	// no feedback loop), so the point can be dragged past the top/bottom edge; the frame refits with
	// headroom on release. This is what lets the extremes (usually the end keys) leave the border.
	return Controller->FrameAlphaToValue(Alpha);
}

FVector2D SPCGExPropertyCurveGraph::KeyToLocal(int32 Index, const FVector2D& Size) const
{
	return FVector2D(TimeToLocalX(Controller->GetKeyTimeAt(Index), Size), ValueToLocalY(Controller->GetKeyValueAt(Index), Size));
}

int32 SPCGExPropertyCurveGraph::HitTestKey(const FVector2D& LocalPos, const FVector2D& Size) const
{
	int32 Best = INDEX_NONE;
	float BestDistSq = HandleHitRadius * HandleHitRadius;

	const int32 Num = Controller->NumKeys();
	for (int32 i = 0; i < Num; ++i)
	{
		const float DistSq = FVector2D::DistSquared(LocalPos, KeyToLocal(i, Size));
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

#pragma endregion

#pragma region Painting

int32 SPCGExPropertyCurveGraph::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (!Controller.IsValid())
	{
		return LayerId;
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetDefaultBrush();

	const float Left = Padding;
	const float Right = Size.X - Padding;
	const float Top = Padding;
	const float Bottom = Size.Y - Padding;

	// Background panel.
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Background);

	// Paint order, back to front: background, grid/border, signed-area fill, key->gem connectors, curve,
	// key markers.
	const int32 GridLayer = LayerId + 1;
	const int32 FillLayer = LayerId + 2;
	const int32 ConnectorLayer = LayerId + 3;
	const int32 CurveLayer = LayerId + 4;
	const int32 KeyLayer = LayerId + 5;

	// Grid lines (vertical at quarters, horizontal at frame min / mid / max).
	for (int32 i = 0; i <= 4; ++i)
	{
		const float X = TimeToLocalX(i * 0.25f, Size);
		TArray<FVector2D> Line;
		Line.Add(FVector2D(X, Top));
		Line.Add(FVector2D(X, Bottom));
		FSlateDrawElement::MakeLines(OutDrawElements, GridLayer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Grid, true, 1.0f);
	}
	for (int32 i = 0; i <= 2; ++i)
	{
		const float Y = Bottom - (i * 0.5f) * FMath::Max(Bottom - Top, 1.0f);
		TArray<FVector2D> Line;
		Line.Add(FVector2D(Left, Y));
		Line.Add(FVector2D(Right, Y));
		FSlateDrawElement::MakeLines(OutDrawElements, GridLayer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Grid, true, 1.0f);
	}

	// Border.
	{
		TArray<FVector2D> Box;
		Box.Add(FVector2D(Left, Top));
		Box.Add(FVector2D(Right, Top));
		Box.Add(FVector2D(Right, Bottom));
		Box.Add(FVector2D(Left, Bottom));
		Box.Add(FVector2D(Left, Top));
		FSlateDrawElement::MakeLines(OutDrawElements, GridLayer, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Border, true, 1.0f);
	}

	// Zero baseline in local space, clamped to the plot so the fill still has a valid edge when value=0
	// falls outside the framed range.
	const float ZeroY = FMath::Clamp(ValueToLocalY(0.0f, Size), Top, Bottom);

	// Evaluated curve, sampled across the whole visible frame (which may be wider than [0,1]) so the
	// extrapolated tails and any out-of-range keys are drawn edge to edge.
	const FRichCurve& RichCurve = Controller->GetCurve();
	{
		const float FrameMinT = Controller->GetTimeFrameMin();
		const float FrameMaxT = Controller->GetTimeFrameMax();
		const float SpanT = FrameMaxT - FrameMinT;

		// Sample times: a uniform grid across the frame, anchored by every in-frame key time plus a
		// just-before-key time. A uniform grid alone straddles the keys, which renders a
		// constant-interp step as a slant one grid cell wide and shifts kinks off their key --
		// no density fixes that. The pre-key sample holds the previous segment's value right up to
		// the key, so steps drop vertically and kinks land exactly on their key at any resolution.
		TArray<float> SampleTimes;
		SampleTimes.Reserve(CurveSamples + 1 + RichCurve.GetNumKeys() * 2);
		for (int32 i = 0; i <= CurveSamples; ++i)
		{
			SampleTimes.Add(FrameMinT + SpanT * (static_cast<float>(i) / static_cast<float>(CurveSamples)));
		}
		for (const FRichCurveKey& Key : RichCurve.GetConstRefOfKeys())
		{
			if (Key.Time <= FrameMinT || Key.Time >= FrameMaxT)
			{
				continue;
			}
			// Sub-pixel in screen space, but large enough to survive float subtraction at the
			// key's own magnitude (relative term covers frames far from t=0).
			const float PreKeyEpsilon = FMath::Max(SpanT * 1.0e-4f, FMath::Abs(Key.Time) * 1.0e-6f);
			SampleTimes.Add(Key.Time - PreKeyEpsilon);
			SampleTimes.Add(Key.Time);
		}
		SampleTimes.Sort();

		TArray<FVector2D> CurvePoints;
		CurvePoints.Reserve(SampleTimes.Num());
		for (const float T : SampleTimes)
		{
			const float V = RichCurve.Eval(T);
			CurvePoints.Add(FVector2D(TimeToLocalX(T, Size), ValueToLocalY(V, Size)));
		}

		// Signed-area fill: one thin quad per sample segment, spanning between the curve and the zero
		// baseline. Local-space boxes reuse the proven white-brush + tint path; a single MakeCustomVerts
		// mesh would be the smoother-but-fiddlier upgrade if this ever needs it.
		for (int32 i = 0; i < CurvePoints.Num() - 1; ++i)
		{
			const float X0 = CurvePoints[i].X;
			const float BoxW = CurvePoints[i + 1].X - X0;
			const float CurveY = (CurvePoints[i].Y + CurvePoints[i + 1].Y) * 0.5f;
			const float BoxTop = FMath::Min(CurveY, ZeroY);
			const float BoxH = FMath::Abs(CurveY - ZeroY);
			if (BoxW > 0.0f && BoxH > 0.0f)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements, FillLayer,
					AllottedGeometry.ToPaintGeometry(FVector2D(BoxW, BoxH), FSlateLayoutTransform(FVector2D(X0, BoxTop))),
					WhiteBrush, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Fill);
			}
		}

		FSlateDrawElement::MakeLines(OutDrawElements, CurveLayer, AllottedGeometry.ToPaintGeometry(), CurvePoints, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::Curve, true, 1.5f);
	}

	// Key markers, each with a vertical drop-line down to its strip gem (dashed + faint when unselected,
	// solid in the selection colour for the selected key).
	// White SVG when supplied (tinted by the marker colour below), else the built-in square.
	const FSlateBrush* KeyBrush = FPCGExPropertiesEditorStyle::GetKeyBrush();
	const FSlateBrush* KeyMarkerBrush = KeyBrush ? KeyBrush : WhiteBrush;
	const int32 SelIdx = Controller->GetSelectedIndex();
	const int32 Num = Controller->NumKeys();
	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D Pos = KeyToLocal(i, Size);
		const bool bSelected = (i == SelIdx);

		// Connector: key point -> plot bottom edge (the strip gem sits directly beneath, same X).
		if (bSelected)
		{
			TArray<FVector2D> Line;
			Line.Add(FVector2D(Pos.X, Pos.Y));
			Line.Add(FVector2D(Pos.X, Bottom));
			FSlateDrawElement::MakeLines(OutDrawElements, ConnectorLayer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::ConnectorSelected, true, 1.0f);
		}
		else
		{
			constexpr float Dash = 3.0f;
			constexpr float DashGap = 2.5f;
			for (float Y = Pos.Y; Y < Bottom; Y += Dash + DashGap)
			{
				TArray<FVector2D> Seg;
				Seg.Add(FVector2D(Pos.X, Y));
				Seg.Add(FVector2D(Pos.X, FMath::Min(Y + Dash, Bottom)));
				FSlateDrawElement::MakeLines(OutDrawElements, ConnectorLayer, AllottedGeometry.ToPaintGeometry(), Seg, ESlateDrawEffect::None, PCGExPropertyCurveGraphColors::ConnectorNormal, true, 1.0f);
			}
		}

		const float MarkerSize = bSelected ? HandleSize + 2.0f : HandleSize;
		const FLinearColor MarkerColor = bSelected ? PCGExPropertyCurveGraphColors::KeySelected : PCGExPropertyCurveGraphColors::KeyNormal;

		FSlateDrawElement::MakeBox(
			OutDrawElements, KeyLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(MarkerSize, MarkerSize), FSlateLayoutTransform(FVector2D(Pos.X - MarkerSize * 0.5f, Pos.Y - MarkerSize * 0.5f))),
			KeyMarkerBrush, ESlateDrawEffect::None, MarkerColor);
	}

	return KeyLayer + 1;
}

#pragma endregion

#pragma region Drag / add hooks

void SPCGExPropertyCurveGraph::ApplyDrag(const FVector2D& Local, const FVector2D& Size, bool bCtrlDown)
{
	// Ctrl locks the horizontal axis: keep the key's current time and edit value only. Read live, so
	// releasing Ctrl mid-drag resumes free 2D movement.
	const float NewTime = bCtrlDown ? Controller->GetKeyTime(DragHandle) : LocalXToTime(Local.X, Size);
	Controller->MoveKey(DragHandle, NewTime, LocalYToValue(Local.Y, Size), /*bInteractive=*/true);
}

void SPCGExPropertyCurveGraph::AddKeyAtCursor(const FVector2D& Local, const FVector2D& Size)
{
	// The graph has a value axis, so Shift+click lands the key exactly at the cursor (X and value).
	Controller->AddKeyAtTimeValue(LocalXToTime(Local.X, Size), LocalYToValue(Local.Y, Size));
}

#pragma endregion
