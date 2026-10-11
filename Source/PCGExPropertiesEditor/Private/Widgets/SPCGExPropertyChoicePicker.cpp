// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SPCGExPropertyChoicePicker.h"

#include "DetailLayoutBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPCGExPropertyChoicePicker"

namespace PCGExPropertyChoicePicker
{
	FText LabelOf(const FPCGExPropertyChoice& Choice)
	{
		return Choice.Label.IsNone() ? LOCTEXT("UnnamedChoice", "(unnamed)") : FText::FromName(Choice.Label);
	}

	FText PreviewOf(const FPCGExPropertyChoice& Choice)
	{
		return PCGExProperties::GetValuePreviewText(FConstStructView(Choice.Value));
	}
}

void SPCGExPropertyChoicePicker::Construct(const FArguments& InArgs, const FStructView InHost, const TSharedRef<IPropertyHandle>& InNotifyHandle)
{
	Host = InHost;
	NotifyHandle = InNotifyHandle;
	bLocked = InArgs._Locked;

	TSharedRef<SWidget> ButtonContent = SNullWidget::NullWidget;
	if (bLocked)
	{
		// Explicit dim, not UseSubduedForeground: the button's foreground carries the arrow's accent.
		FLinearColor Subdued = FStyleColors::Foreground.GetSpecifiedColor();
		Subdued.A *= 0.6f;

		ButtonContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &SPCGExPropertyChoicePicker::GetSelectionLabel)
				.ColorAndOpacity(this, &SPCGExPropertyChoicePicker::GetSelectionColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text(this, &SPCGExPropertyChoicePicker::GetSelectionPreview)
				.ColorAndOpacity(FSlateColor(Subdued))
			];
	}

	ChildSlot
	[
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), bLocked ? "ComboButton" : "SimpleComboButton")
		.ForegroundColor(this, &SPCGExPropertyChoicePicker::GetArrowColor)
		.HasDownArrow(true)
		.ToolTipText(this, &SPCGExPropertyChoicePicker::GetSelectionToolTip)
		.OnGetMenuContent(this, &SPCGExPropertyChoicePicker::BuildMenu)
		.ButtonContent()
		[
			ButtonContent
		]
	];
}

const FPCGExProperty* SPCGExPropertyChoicePicker::GetHost() const
{
	return Host.IsValid() ? Host.GetPtr<FPCGExProperty>() : nullptr;
}

int32 SPCGExPropertyChoicePicker::GetSelectedIndex() const
{
	const FPCGExProperty* Property = GetHost();
	return Property ? PCGExProperties::ResolveSelectedChoice(FConstStructView(Host), Property->Choices) : INDEX_NONE;
}

FText SPCGExPropertyChoicePicker::GetSelectionLabel() const
{
	const FPCGExProperty* Property = GetHost();
	const int32 Selected = GetSelectedIndex();
	if (!Property || Selected == INDEX_NONE)
	{
		return LOCTEXT("CustomValue", "Custom");
	}
	return PCGExPropertyChoicePicker::LabelOf(Property->Choices.Items[Selected]);
}

FText SPCGExPropertyChoicePicker::GetSelectionPreview() const
{
	return Host.IsValid() ? PCGExProperties::GetValuePreviewText(FConstStructView(Host)) : FText::GetEmpty();
}

int32 SPCGExPropertyChoicePicker::GetBoundIndex() const
{
	const FPCGExProperty* Property = GetHost();
	const int32 Chosen = Property ? PCGExProperties::FindChoiceById(Property->Choices, Property->ChosenChoiceId) : INDEX_NONE;
	return Chosen != INDEX_NONE && PCGExProperties::IsChoiceCompatible(FConstStructView(Host), Property->Choices.Items[Chosen]) ? Chosen : INDEX_NONE;
}

FSlateColor SPCGExPropertyChoicePicker::GetSelectionColor() const
{
	// Explicit, not UseForeground: the button's foreground carries the arrow's accent and must not bleed into the label.
	return GetSelectedIndex() == INDEX_NONE ? FStyleColors::Warning : FStyleColors::Foreground;
}

FSlateColor SPCGExPropertyChoicePicker::GetArrowColor() const
{
	return GetBoundIndex() != INDEX_NONE ? FStyleColors::Primary : FSlateColor::UseStyle();
}

FText SPCGExPropertyChoicePicker::GetSelectionToolTip() const
{
	if (!bLocked)
	{
		const int32 Bound = GetBoundIndex();
		if (Bound != INDEX_NONE)
		{
			return FText::Format(
				LOCTEXT("BoundToolTip", "Bound to '{0}': the value follows this choice and detaches when edited."),
				PCGExPropertyChoicePicker::LabelOf(GetHost()->Choices.Items[Bound]));
		}
		return LOCTEXT("CustomToolTipUnlocked", "Custom value. Pick a choice to bind the value to it.");
	}
	return GetSelectedIndex() == INDEX_NONE
		? LOCTEXT("CustomToolTip", "This value is not one of the choices. Pick one to align it; the value is kept as is until then.")
		: LOCTEXT("LockedToolTip", "This property is restricted to its choices.");
}

TSharedRef<SWidget> SPCGExPropertyChoicePicker::BuildMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	const FPCGExProperty* Property = GetHost();
	if (!Property || Property->Choices.Items.IsEmpty())
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock).Text(LOCTEXT("NoChoices", "No choices")).Font(IDetailLayoutBuilder::GetDetailFont()),
			FText::GetEmpty());
		return MenuBuilder.MakeWidget();
	}

	const int32 Selected = GetSelectedIndex();
	for (int32 i = 0; i < Property->Choices.Items.Num(); ++i)
	{
		const FPCGExPropertyChoice& Choice = Property->Choices.Items[i];
		const bool bCompatible = PCGExProperties::IsChoiceCompatible(FConstStructView(Host), Choice);
		const bool bSelected = (i == Selected);

		FUIAction Action(
			FExecuteAction::CreateSP(this, &SPCGExPropertyChoicePicker::PickChoice, Choice.Id),
			FCanExecuteAction::CreateLambda([bCompatible]() { return bCompatible; }),
			FIsActionChecked::CreateLambda([bSelected]() { return bSelected; }));

		const TSharedRef<SWidget> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(PCGExPropertyChoicePicker::LabelOf(Choice))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(16, 0, 0, 0)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text(PCGExPropertyChoicePicker::PreviewOf(Choice))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];

		MenuBuilder.AddMenuEntry(
			Action,
			Row,
			NAME_None,
			bCompatible
				? PCGExPropertyChoicePicker::PreviewOf(Choice)
				: LOCTEXT("IncompatibleChoice", "This choice no longer matches the property's type."),
			EUserInterfaceActionType::Check);
	}

	return MenuBuilder.MakeWidget();
}

void SPCGExPropertyChoicePicker::PickChoice(const FGuid ChoiceId)
{
	const FPCGExProperty* Property = GetHost();
	if (!Property || !NotifyHandle.IsValid())
	{
		return;
	}
	// Re-resolved by Id: the list may have changed between the menu's build and the click.
	const int32 Index = PCGExProperties::FindChoiceById(Property->Choices, ChoiceId);
	if (Index == INDEX_NONE || !PCGExProperties::IsChoiceCompatible(FConstStructView(Host), Property->Choices.Items[Index]))
	{
		return;
	}

	// Same bracket a typed edit produces: owner dirty hooks and refreshes ride NotifyPostChange.
	FScopedTransaction Transaction(LOCTEXT("PickChoice", "Pick Choice"));
	NotifyHandle->NotifyPreChange();
	PCGExProperties::ApplyChoice(Host, Property->Choices.Items[Index]);
	NotifyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	NotifyHandle->NotifyFinishedChangingProperties();
}

#undef LOCTEXT_NAMESPACE
