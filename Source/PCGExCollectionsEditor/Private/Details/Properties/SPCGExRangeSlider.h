// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

DECLARE_DELEGATE_OneParam(FPCGExOnRangeChanged, const FVector2D& /*NewRange*/);

namespace PCGExRangeSlider
{
	/** Range with start <= end, both inside 0..1. */
	inline FVector2D ConformRange(const FVector2D& Range)
	{
		return FVector2D(
			FMath::Clamp(FMath::Min(Range.X, Range.Y), 0.0, 1.0),
			FMath::Clamp(FMath::Max(Range.X, Range.Y), 0.0, 1.0));
	}
}

/**
 * Two-handle range slider over 0..1: the cap on each end moves that end, the bar between them moves both and
 * keeps their distance. Knows nothing about properties -- the owner supplies the range and receives the edits.
 *
 * A drag is one gesture: OnBeginDrag, one or more OnValueChanged, then exactly one OnEndDrag -- also on
 * capture loss, so the owner can always close what OnBeginDrag opened. A press that never changes the value
 * fires nothing, which is what keeps a double-click from nudging it.
 *
 * The value is drawn conformed to 0..1; the first edit is what writes the conformed range back.
 */
class SPCGExRangeSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExRangeSlider)
		{
		}

		/** X = start, Y = end, as 0..1 positions. Unset draws the bare track and disables dragging. */
		SLATE_ATTRIBUTE(TOptional<FVector2D>, Value)

		SLATE_EVENT(FSimpleDelegate, OnBeginDrag)
		SLATE_EVENT(FPCGExOnRangeChanged, OnValueChanged)
		SLATE_EVENT(FPCGExOnRangeChanged, OnEndDrag)
		SLATE_EVENT(FSimpleDelegate, OnDoubleClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ Begin SWidget interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	//~ End SWidget interface

private:
	enum class EPart : uint8
	{
		None,
		Start,
		End,
		Body,
	};

	/** Where the current value sits along a widget of a given width. */
	struct FLayout
	{
		/** The value conformed to 0..1. */
		FVector2D Range = FVector2D(0.0, 1.0);

		/** Local X of the range's start and end; the caps sit outside this span. */
		float StartX = 0.0f;
		float EndX = 0.0f;

		/** Pixels that 0..1 maps onto. */
		float TrackWidth = 1.0f;

		bool bHasValue = false;
	};

	FLayout ComputeLayout(float Width) const;
	EPart HitTest(float LocalX, const FLayout& Layout) const;

	/** PressValue moved by Travel pixels along the pressed part, snapped and kept inside 0..1. */
	FVector2D ComputeDraggedRange(float Travel, const FLayout& Layout) const;

	/** Ends the press; fires OnEndDrag when the press had changed the value. */
	void EndPress();

	void SetHoveredPart(EPart InPart);

	TAttribute<TOptional<FVector2D>> Value;

	FSimpleDelegate OnBeginDrag;
	FPCGExOnRangeChanged OnValueChanged;
	FPCGExOnRangeChanged OnEndDrag;
	FSimpleDelegate OnDoubleClicked;

	EPart HoveredPart = EPart::None;
	EPart PressedPart = EPart::None;

	/** The press travelled past the drag trigger distance. */
	bool bPastThreshold = false;
	/** OnBeginDrag has fired and OnEndDrag is owed. */
	bool bGestureOpen = false;

	float PressLocalX = 0.0f;
	/** Conformed range at press time, and the last range sent out. */
	FVector2D PressValue = FVector2D::ZeroVector;
	FVector2D DragValue = FVector2D::ZeroVector;

	static constexpr float CapWidth = 7.0f;
	static constexpr float BarHeight = 14.0f;
	static constexpr float TickWidth = 2.0f;
	static constexpr float DesiredWidth = 160.0f;
	static constexpr float DesiredHeight = 18.0f;
};
