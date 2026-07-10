// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#include "SPCGExPropertyCurveKeyStrip.h"

#include "PCGExPropertiesEditorStyle.h"
#include "PCGExPropertyCurveEditController.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PCGExPropertyCurveStripColors
{
	static constexpr FLinearColor Background(0.0f, 0.0f, 0.0f, 0.2f);
	static constexpr FLinearColor Track(1.0f, 1.0f, 1.0f, 0.1f);
	static constexpr FLinearColor GemNormal(0.8f, 0.8f, 0.85f, 1.0f);
	static constexpr FLinearColor GemSelected(1.0f, 0.6f, 0.1f, 1.0f);
}

void SPCGExPropertyCurveKeyStrip::Construct(const FArguments& InArgs, const TSharedRef<FPCGExPropertyCurveEditController>& InController)
{
	Init(InController);
}

FVector2D SPCGExPropertyCurveKeyStrip::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}

#pragma region Hit-test

int32 SPCGExPropertyCurveKeyStrip::HitTestKey(const FVector2D& Local, const FVector2D& Size) const
{
	int32 Best = INDEX_NONE;
	float BestDist = GemHitHalfWidth;

	const int32 Num = Controller->NumKeys();
	for (int32 i = 0; i < Num; ++i)
	{
		const float Dist = FMath::Abs(Local.X - TimeToLocalX(Controller->GetKeyTimeAt(i), Size));
		if (Dist <= BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
	}
	return Best;
}

#pragma endregion

#pragma region Painting

int32 SPCGExPropertyCurveKeyStrip::OnPaint(
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

	// Background.
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush, ESlateDrawEffect::None, PCGExPropertyCurveStripColors::Background);

	// Centre track line.
	{
		const float Y = Size.Y * 0.5f;
		TArray<FVector2D> Line;
		Line.Add(FVector2D(Padding, Y));
		Line.Add(FVector2D(Size.X - Padding, Y));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, PCGExPropertyCurveStripColors::Track, true, 1.0f);
	}

	// Gems. White SVG when supplied (tinted per selection below), else the built-in rectangle.
	const FSlateBrush* SliderBrush = FPCGExPropertiesEditorStyle::GetSliderBrush();
	const FSlateBrush* GemDrawBrush = SliderBrush ? SliderBrush : WhiteBrush;
	const int32 GemLayer = LayerId + 2;
	const float GemHeight = FMath::Max(Size.Y - 6.0f, 4.0f);
	const float GemTop = (Size.Y - GemHeight) * 0.5f;

	const int32 SelIdx = Controller->GetSelectedIndex();
	const int32 Num = Controller->NumKeys();
	for (int32 i = 0; i < Num; ++i)
	{
		const float CenterX = TimeToLocalX(Controller->GetKeyTimeAt(i), Size);

		const bool bSelected = (i == SelIdx);

		const FLinearColor GemColor = bSelected ? PCGExPropertyCurveStripColors::GemSelected : PCGExPropertyCurveStripColors::GemNormal;

		FSlateDrawElement::MakeBox(
			OutDrawElements, GemLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(GemWidth, GemHeight), FSlateLayoutTransform(FVector2D(CenterX - GemWidth * 0.5f, GemTop))),
			GemDrawBrush, ESlateDrawEffect::None, GemColor);
	}

	return GemLayer + 1;
}

#pragma endregion

#pragma region Drag / add hooks

void SPCGExPropertyCurveKeyStrip::ApplyDrag(const FVector2D& Local, const FVector2D& Size, bool /*bCtrlDown*/)
{
	// X only: keep the current value (there is no vertical axis and no Ctrl axis-lock on the strip).
	Controller->MoveKey(DragHandle, LocalXToTime(Local.X, Size), Controller->GetKeyValue(DragHandle), /*bInteractive=*/true);
}

void SPCGExPropertyCurveKeyStrip::AddKeyAtCursor(const FVector2D& Local, const FVector2D& Size)
{
	// The strip has no Y, so Shift+click adds at the cursor time with the value sampled from the curve.
	Controller->AddKeyAtTime(LocalXToTime(Local.X, Size));
}

#pragma endregion
