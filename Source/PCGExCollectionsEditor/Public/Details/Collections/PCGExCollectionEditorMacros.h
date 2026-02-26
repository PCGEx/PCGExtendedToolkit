// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#define PCGEX_COMBOBOX_INTERPUNCT \
SNew(STextBlock)\
.Text(FText::FromString(TEXT("\u00B7\u00B7\u00B7")))\
.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))

#define PCGEX_COMBOBOX_BUTTON_CONTENT(_BRUSH) \
SNew(SHorizontalBox)\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)\
[\
	SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)\
	[\
		SNew(SImage).Image(FAppStyle::Get().GetBrush(_BRUSH))\
	]\
]\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)\
[\
	PCGEX_COMBOBOX_INTERPUNCT\
]

#define PCGEX_COMBOBOX_BUTTON_CONTENT_TEXT(_TEXT, _FONT_SIZE) \
SNew(SHorizontalBox)\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)\
[\
	SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)\
	[\
		SNew(STextBlock)\
		.Text(FText::FromString(_TEXT))\
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", _FONT_SIZE))\
	]\
]\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)\
[\
	PCGEX_COMBOBOX_INTERPUNCT\
]
