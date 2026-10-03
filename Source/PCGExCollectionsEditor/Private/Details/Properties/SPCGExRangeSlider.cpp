// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/SPCGExRangeSlider.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"

namespace PCGExRangeSlider
{
	/** Outlined bar spanning cap to cap. The fill is tinted per draw; the outline color has to live in the brush. */
	const FSlateBrush* GetHandleBrush(const bool bHot)
	{
		// FSlateDrawElement::MakeBox takes a rounded box's outline color from the brush, not from the draw tint.
		static const FSlateRoundedBoxBrush Normal(FLinearColor::White, 2.0f, FStyleColors::Primary, 1.0f);
		static const FSlateRoundedBoxBrush Hot(FLinearColor::White, 2.0f, FStyleColors::PrimaryHover, 1.0f);
		return bHot ? &Hot : &Normal;
	}

	/** InValue rounded to the coarsest power of ten that is still finer than one pixel of travel. */
	double SnapToPixelStep(const double InValue, const double Span, const float TrackWidth)
	{
		const double PerPixel = Span / FMath::Max(TrackWidth, 1.0f);
		if (!FMath::IsFinite(PerPixel) || PerPixel <= 0.0)
		{
			return InValue;
		}

		const int32 Exponent = FMath::Clamp(FMath::FloorToInt32(FMath::LogX(10.0, PerPixel)), -12, 12);

		// An exact integer power, applied as such and never as its inexact reciprocal: the result is the double
		// nearest the decimal, identically on every platform.
		double Scale = 1.0;
		for (int32 i = FMath::Abs(Exponent); i > 0; --i)
		{
			Scale *= 10.0;
		}

		return Exponent < 0
			? FMath::RoundToDouble(InValue * Scale) / Scale
			: FMath::RoundToDouble(InValue / Scale) * Scale;
	}

	/** InValue moved onto the step grid when it already sits there up to rounding error, else unchanged. */
	double FoldToPixelStep(const double InValue, const double Span, const float TrackWidth)
	{
		const double Snapped = SnapToPixelStep(InValue, Span, TrackWidth);
		return FMath::IsNearlyEqual(Snapped, InValue, Span * 1.e-9) ? Snapped : InValue;
	}
}

void SPCGExRangeSlider::Construct(const FArguments& InArgs)
{
	Value = InArgs._Value;
	Bounds = InArgs._Bounds;
	OnBeginDrag = InArgs._OnBeginDrag;
	OnValueChanged = InArgs._OnValueChanged;
	OnEndDrag = InArgs._OnEndDrag;
	OnDoubleClicked = InArgs._OnDoubleClicked;
}

FVector2D SPCGExRangeSlider::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}

#pragma region Layout

SPCGExRangeSlider::FLayout SPCGExRangeSlider::ComputeLayout(const float Width) const
{
	FLayout Layout;
	Layout.TrackWidth = FMath::Max(Width - CapWidth * 2.0f, 1.0f);

	const TOptional<FVector2D> RawValue = Value.Get(TOptional<FVector2D>());
	const FVector2D RawBounds = Bounds.Get(FVector2D(0.0, 1.0));
	if (!RawValue.IsSet() || RawValue->ContainsNaN() || RawBounds.ContainsNaN())
	{
		return Layout;
	}

	const FVector2D Ordered = PCGExRangeSlider::OrderBounds(RawBounds);
	const double Span = Ordered.Y - Ordered.X;
	if (!FMath::IsFinite(Span))
	{
		return Layout;
	}

	Layout.Bounds = Ordered;
	Layout.Range = PCGExRangeSlider::ConformRange(*RawValue, Ordered);
	Layout.bHasValue = true;

	const auto ToLocalX = [&Layout, Span](const double InValue)
	{
		const float Alpha = Span > 0.0 ? static_cast<float>((InValue - Layout.Bounds.X) / Span) : 0.0f;
		return CapWidth + Alpha * Layout.TrackWidth;
	};
	Layout.StartX = ToLocalX(Layout.Range.X);
	Layout.EndX = ToLocalX(Layout.Range.Y);
	return Layout;
}

SPCGExRangeSlider::EPart SPCGExRangeSlider::HitTest(const float LocalX, const FLayout& Layout) const
{
	if (!Layout.bHasValue || LocalX < Layout.StartX - CapWidth || LocalX > Layout.EndX + CapWidth)
	{
		return EPart::None;
	}
	if (LocalX < Layout.StartX)
	{
		return EPart::Start;
	}
	return LocalX > Layout.EndX ? EPart::End : EPart::Body;
}

FVector2D SPCGExRangeSlider::ComputeDraggedRange(const float Travel, const FLayout& Layout) const
{
	using namespace PCGExRangeSlider;

	const double Lo = Layout.Bounds.X;
	const double Hi = Layout.Bounds.Y;
	const double Span = Hi - Lo;
	const double Delta = static_cast<double>(Travel) / Layout.TrackWidth * Span;

	FVector2D Result = PressValue;
	switch (PressedPart)
	{
	case EPart::Start:
		Result.X = FMath::Clamp(SnapToPixelStep(PressValue.X + Delta, Span, Layout.TrackWidth), Lo, PressValue.Y);
		break;
	case EPart::End:
		Result.Y = FMath::Clamp(SnapToPixelStep(PressValue.Y + Delta, Span, Layout.TrackWidth), PressValue.X, Hi);
		break;
	case EPart::Body:
		{
			// The length is kept as-is, never snapped. Hi - Length and X + Length can land a rounding error off the
			// step grid; folding those back keeps both ends short decimals.
			const double Length = PressValue.Y - PressValue.X;
			const double ClampedStart = FMath::Clamp(SnapToPixelStep(PressValue.X + Delta, Span, Layout.TrackWidth), Lo, Hi - Length);
			Result.X = FMath::Max(FoldToPixelStep(ClampedStart, Span, Layout.TrackWidth), Lo);
			Result.Y = FMath::Min(FoldToPixelStep(Result.X + Length, Span, Layout.TrackWidth), Hi);
		}
		break;
	default:
		break;
	}
	return Result;
}

#pragma endregion

#pragma region Painting

int32 SPCGExRangeSlider::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FLayout Layout = ComputeLayout(Size.X);

	// A disabled row (override toggled off) paints dimmed, like any other disabled Slate widget.
	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush(TEXT("WhiteBrush"));

	const auto DrawBox = [&](const int32 Layer, const float X, const float Y, const float W, const float H, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			Brush, DrawEffects, Color * WidgetTint);
	};

	const float BarTop = FMath::RoundToFloat((Size.Y - BarHeight) * 0.5f);
	const float MidY = FMath::RoundToFloat(Size.Y * 0.5f);

	// Track: a hairline between two end ticks, the ticks standing for the bounds.
	const FLinearColor TrackColor = FStyleColors::Foreground.GetSpecifiedColor().CopyWithNewOpacity(0.5f);
	DrawBox(LayerId, 0.0f, MidY, Size.X, 1.0f, WhiteBrush, TrackColor);
	DrawBox(LayerId, 0.0f, BarTop, TickWidth, BarHeight, WhiteBrush, TrackColor);
	DrawBox(LayerId, Size.X - TickWidth, BarTop, TickWidth, BarHeight, WhiteBrush, TrackColor);

	if (!Layout.bHasValue)
	{
		return LayerId;
	}

	const EPart ActivePart = PressedPart != EPart::None ? PressedPart : HoveredPart;

	// Handle: one outlined bar from cap to cap; the range itself is the span between the caps.
	const float HandleLeft = FMath::RoundToFloat(Layout.StartX - CapWidth);
	const float HandleRight = FMath::RoundToFloat(Layout.EndX + CapWidth);
	const bool bBodyActive = ActivePart == EPart::Body;
	const FLinearColor BarColor = FMath::Lerp(
		FStyleColors::Input.GetSpecifiedColor(), FStyleColors::Primary.GetSpecifiedColor(), bBodyActive ? 0.6f : 0.4f);
	DrawBox(LayerId + 1, HandleLeft, BarTop, HandleRight - HandleLeft, BarHeight, PCGExRangeSlider::GetHandleBrush(bBodyActive), BarColor);

	// Caps, inset by the handle's outline.
	const FLinearColor CapColor = FStyleColors::Foreground.GetSpecifiedColor();
	const FLinearColor CapActiveColor = FStyleColors::ForegroundHover.GetSpecifiedColor();
	DrawBox(LayerId + 2, HandleLeft + 1.0f, BarTop + 1.0f, CapWidth - 1.0f, BarHeight - 2.0f, WhiteBrush, ActivePart == EPart::Start ? CapActiveColor : CapColor);
	DrawBox(LayerId + 2, HandleRight - CapWidth, BarTop + 1.0f, CapWidth - 1.0f, BarHeight - 2.0f, WhiteBrush, ActivePart == EPart::End ? CapActiveColor : CapColor);

	return LayerId + 2;
}

#pragma endregion

#pragma region Input

FReply SPCGExRangeSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || PressedPart != EPart::None)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Size = MyGeometry.GetLocalSize();
	const FLayout Layout = ComputeLayout(Size.X);

	// The bare track is inert: only the caps and the bar start a press.
	const EPart Part = HitTest(Local.X, Layout);
	if (Part == EPart::None)
	{
		return FReply::Unhandled();
	}

	PressedPart = Part;
	bPastThreshold = false;
	bGestureOpen = false;
	PressLocalX = Local.X;
	PressValue = Layout.Range;
	DragValue = Layout.Range;
	Invalidate(EInvalidateWidgetReason::Paint);

	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPCGExRangeSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Size = MyGeometry.GetLocalSize();
	const FLayout Layout = ComputeLayout(Size.X);

	if (PressedPart == EPart::None || !HasMouseCapture())
	{
		SetHoveredPart(HitTest(Local.X, Layout));
		return FReply::Unhandled();
	}

	const float Travel = Local.X - PressLocalX;
	if (!bPastThreshold)
	{
		if (FMath::Abs(Travel) < FSlateApplication::Get().GetDragTriggerDistance())
		{
			return FReply::Handled();
		}
		bPastThreshold = true;
	}

	if (!Layout.bHasValue)
	{
		return FReply::Handled();
	}

	const FVector2D NewValue = ComputeDraggedRange(Travel, Layout);
	if (NewValue != DragValue)
	{
		// Opened on the first actual change, so a press that goes nowhere never reaches the owner.
		if (!bGestureOpen)
		{
			bGestureOpen = true;
			OnBeginDrag.ExecuteIfBound();
		}
		DragValue = NewValue;
		OnValueChanged.ExecuteIfBound(NewValue);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return FReply::Handled();
}

FReply SPCGExRangeSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PressedPart != EPart::None)
	{
		EndPress();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SPCGExRangeSlider::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// A stolen capture (window deactivation, a popup) delivers no button-up; the gesture still has to close.
	if (PressedPart != EPart::None)
	{
		EndPress();
	}
}

void SPCGExRangeSlider::EndPress()
{
	// Cleared before anything else: FSlateUser::ReleaseCapture calls OnMouseCaptureLost on a voluntary release too.
	const bool bCloseGesture = bGestureOpen;
	PressedPart = EPart::None;
	bPastThreshold = false;
	bGestureOpen = false;
	Invalidate(EInvalidateWidgetReason::Paint);

	if (bCloseGesture)
	{
		OnEndDrag.ExecuteIfBound(DragValue);
	}
}

FReply SPCGExRangeSlider::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnDoubleClicked.ExecuteIfBound())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SPCGExRangeSlider::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	SetHoveredPart(EPart::None);
}

void SPCGExRangeSlider::SetHoveredPart(const EPart InPart)
{
	if (HoveredPart != InPart)
	{
		HoveredPart = InPart;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FCursorReply SPCGExRangeSlider::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	switch (PressedPart != EPart::None ? PressedPart : HoveredPart)
	{
	case EPart::Start:
	case EPart::End:
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	case EPart::Body:
		return FCursorReply::Cursor(PressedPart == EPart::Body ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
	default:
		return FCursorReply::Unhandled();
	}
}

#pragma endregion
