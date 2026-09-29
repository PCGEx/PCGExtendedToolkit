// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPartitionGridCustomization.h"

#include "DetailLayoutBuilder.h"
#include "PropertyHandle.h"
#include "Elements/Utils/PCGExPartitionIdentifier.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionGridCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGExPartitionGridCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPartitionGridCustomization());
}

TSharedRef<SWidget> FPCGExPartitionGridCustomization::MakeNameWidget(const TSharedRef<IPropertyHandle>& PropertyHandle)
{
	return MakeSuffixWidget(PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionGrid, Suffix)));
}

TSharedRef<SWidget> FPCGExPartitionGridCustomization::MakeSuffixWidget(const TSharedPtr<IPropertyHandle>& SuffixHandle)
{
	const TSharedRef<SEditableTextBox> TextBox = SNew(SEditableTextBox)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text_Lambda([SuffixHandle]()
		{
			FName Value = NAME_None;
			return SuffixHandle->GetValue(Value) == FPropertyAccess::Success && !Value.IsNone()
				       ? FText::FromName(Value)
				       : FText::GetEmpty();
		})
		.OnTextCommitted_Lambda([SuffixHandle](const FText& NewText, ETextCommit::Type CommitType)
		{
			if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus) { return; }
			const FString Trimmed = NewText.ToString().TrimStartAndEnd();
			SuffixHandle->SetValue(Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed));
		});

	// Placeholder shows only while the suffix is None AND the box isn't being edited (the live edit
	// text would otherwise sit under it). HitTestInvisible so clicks fall through to the box.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			TextBox
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Center)
		.Padding(6, 0)
		[
			SNew(STextBlock)
			.Visibility_Lambda([SuffixHandle, TextBox]()
			{
				FName Value = NAME_None;
				const bool bIsNone = SuffixHandle->GetValue(Value) != FPropertyAccess::Success || Value.IsNone();
				const bool bEditing = TextBox->HasKeyboardFocus() || TextBox->HasFocusedDescendants();
				return bIsNone && !bEditing ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Text(LOCTEXT("SuffixSelfPlaceholder", "Self"))
			.Font(GetHintFont())
			.ColorAndOpacity(GetHintColor())
		];
}

#undef LOCTEXT_NAMESPACE
