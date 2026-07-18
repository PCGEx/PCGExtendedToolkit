// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExCollectionEditorSlateUtils
{
	/** Rounded badge brush shared by collection grid tiles. Function-local static: one brush
	 *  per process, so SBorder can safely hold the pointer. White fill is intentional --
	 *  per-badge BorderBackgroundColor applies a multiplicative tint. */
	inline const FSlateBrush* GetBadgeBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.0f);
		return &Brush;
	}

	inline TSharedRef<SWidget> MakeSmallLabel(const FString& Text)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			.MinDesiredWidth(20);
	}

	/** Pick black or white text to remain legible against a linear-space background color.
	 *  sqrt approximates sRGB encoding, Rec.601 coefficients then map to perceived brightness.
	 *  FLinearColor::GetLuminance uses raw linear values which under-weights mid-tones for UI legibility. */
	inline FSlateColor PickReadableTextColor(const FLinearColor& Bg)
	{
		const float Perceived =
			0.299f * FMath::Sqrt(FMath::Max(0.f, Bg.R)) +
			0.587f * FMath::Sqrt(FMath::Max(0.f, Bg.G)) +
			0.114f * FMath::Sqrt(FMath::Max(0.f, Bg.B));
		return FSlateColor(Perceived > 0.6f ? FLinearColor::Black : FLinearColor::White);
	}
}
