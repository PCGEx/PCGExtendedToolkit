// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExDetailRowWidgets.h"

#include "DetailLayoutBuilder.h"
#include "Styling/SlateColor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExDetailRowWidgets
{
	TSharedRef<SWidget> MakeArrowedHintTextBox(
		const TAttribute<FText>& Text,
		const TAttribute<FText>& HintText,
		const FOnTextCommitted& OnCommitted,
		const TAttribute<bool>& IsEnabled)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 4, 0)
			[
				SNew(STextBlock)
				                .Text(FText::FromString(TEXT("\x2192"))) // -> arrow
				                .Font(IDetailLayoutBuilder::GetDetailFont())
				                .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled(IsEnabled)
				[
					SNew(SEditableTextBox)
					.Text(Text)
					.HintText(HintText)
					.OnTextCommitted(OnCommitted)
					.SelectAllTextWhenFocused(true)
					.ClearKeyboardFocusOnCommit(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];
	}
}
