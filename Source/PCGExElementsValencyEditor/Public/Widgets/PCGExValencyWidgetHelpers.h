// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Colors/SColorBlock.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExValencyConnectorSet.h"

/**
 * Shared helper functions for Valency editor widgets.
 * Static, dependency-free UI building blocks.
 */
namespace PCGExValencyWidgets
{
	/** Centralized style constants for all Valency editor widgets. */
	namespace Style
	{
		// Fonts
		inline FSlateFontInfo Label()     { return FCoreStyle::GetDefaultFontStyle("Regular", 8); }
		inline FSlateFontInfo Bold()      { return FCoreStyle::GetDefaultFontStyle("Bold", 8); }
		inline FSlateFontInfo Title()     { return FCoreStyle::GetDefaultFontStyle("Bold", 9); }
		inline FSlateFontInfo Italic()    { return FCoreStyle::GetDefaultFontStyle("Italic", 8); }
		inline FSlateFontInfo Small()     { return FCoreStyle::GetDefaultFontStyle("Regular", 7); }
		inline FSlateFontInfo SmallBold() { return FCoreStyle::GetDefaultFontStyle("Bold", 7); }

		// Colors
		inline FSlateColor LabelColor()  { return FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)); }
		inline FSlateColor DimColor()    { return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)); }
		inline FSlateColor AccentColor() { return FSlateColor(FLinearColor(0.8f, 0.5f, 0.1f)); }

		// Dimensions
		constexpr float LabelWidth = 100.0f;
		constexpr float SwatchSize = 16.0f;
		constexpr float RowPadding = 2.0f;
		constexpr float SectionGap = 10.0f;
	}

	/** Generic labeled control: [SBox W=LabelWidth][Label] + [FillWidth][ControlWidget] */
	inline TSharedRef<SWidget> MakeLabeledControl(const FText& Label, TSharedRef<SWidget> ControlWidget)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(Style::LabelWidth)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(Style::Label())
					.ColorAndOpacity(Style::LabelColor())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				ControlWidget
			];
	}

	/** Create a labeled row: left label (dimmed, LabelWidth) + right value text */
	inline TSharedRef<SWidget> MakeLabeledRow(const FText& Label, const FText& Value)
	{
		return MakeLabeledControl(Label,
			SNew(STextBlock)
			.Text(Value)
			.Font(Style::Label())
		);
	}

	/** Create a labeled color swatch row */
	inline TSharedRef<SWidget> MakeLabeledColorRow(const FText& Label, const FLinearColor& Color)
	{
		return MakeLabeledControl(Label,
			SNew(SColorBlock)
			.Color(Color)
			.Size(FVector2D(Style::SwatchSize, Style::SwatchSize))
		);
	}

	/** Create a bold section header */
	inline TSharedRef<SWidget> MakeSectionHeader(const FText& Title)
	{
		return SNew(STextBlock)
			.Text(Title)
			.Font(Style::Bold())
			.Margin(FMargin(0, 2, 0, 1));
	}

	/** Create hint text (italic, dim, no label column) */
	inline TSharedRef<SWidget> MakeHintText(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(Style::Italic())
			.ColorAndOpacity(Style::DimColor());
	}

	/** Create a checkbox row: [checkbox] + label text (no label column — checkbox IS the control) */
	inline TSharedRef<SWidget> MakeCheckboxRow(
		const FText& Label,
		const FText& Tooltip,
		bool bValue,
		TFunction<void(bool)> OnChanged)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.IsChecked(bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.ToolTipText(Tooltip)
				.OnCheckStateChanged_Lambda([OnChanged = MoveTemp(OnChanged)](ECheckBoxState NewState)
				{
					OnChanged(NewState == ECheckBoxState::Checked);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Style::Label())
				.ColorAndOpacity(Style::LabelColor())
			];
	}

	/** Create a labeled float spinbox row */
	inline TSharedRef<SWidget> MakeLabeledSpinBox(
		const FText& Label,
		float Value,
		float MinValue,
		float Delta,
		const FText& Tooltip,
		TFunction<void(float)> OnCommitted)
	{
		return MakeLabeledControl(Label,
			SNew(SSpinBox<float>)
			.Value(Value)
			.MinValue(MinValue)
			.Delta(Delta)
			.Font(Style::Label())
			.ToolTipText(Tooltip)
			.OnValueCommitted_Lambda([OnCommitted = MoveTemp(OnCommitted)](float NewValue, ETextCommit::Type)
			{
				OnCommitted(NewValue);
			})
		);
	}

	/** Create a labeled integer spinbox row */
	inline TSharedRef<SWidget> MakeLabeledIntSpinBox(
		const FText& Label,
		int32 Value,
		int32 MinValue,
		const FText& Tooltip,
		TFunction<void(int32)> OnCommitted)
	{
		return MakeLabeledControl(Label,
			SNew(SSpinBox<int32>)
			.Value(Value)
			.MinValue(MinValue)
			.Font(Style::Label())
			.ToolTipText(Tooltip)
			.OnValueCommitted_Lambda([OnCommitted = MoveTemp(OnCommitted)](int32 NewValue, ETextCommit::Type)
			{
				OnCommitted(NewValue);
			})
		);
	}

	/** Create a labeled editable text field row */
	inline TSharedRef<SWidget> MakeLabeledTextField(
		const FText& Label,
		const FText& Value,
		const FText& Hint,
		const FText& Tooltip,
		TFunction<void(const FText&)> OnCommitted)
	{
		return MakeLabeledControl(Label,
			SNew(SEditableTextBox)
			.Text(Value)
			.HintText(Hint)
			.Font(Style::Label())
			.ToolTipText(Tooltip)
			.OnTextCommitted_Lambda([OnCommitted = MoveTemp(OnCommitted)](const FText& NewText, ETextCommit::Type)
			{
				OnCommitted(NewText);
			})
		);
	}

	/** Create a Rebuild All button */
	inline TSharedRef<SWidget> MakeRebuildAllButton(UPCGExValencyCageEditorMode* EditorMode)
	{
		TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

		return SNew(SButton)
			.Text(NSLOCTEXT("PCGExValency", "RebuildAll", "Rebuild All"))
			.ToolTipText(NSLOCTEXT("PCGExValency", "RebuildAllTip", "Rebuild all cages in the scene"))
			.ContentPadding(FMargin(4, 1))
			.OnClicked_Lambda([WeakMode]() -> FReply
			{
				if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
				{
					for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : Mode->GetCachedCages())
					{
						if (APCGExValencyCageBase* Cage = CagePtr.Get())
						{
							Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					}
				}
				return FReply::Handled();
			});
	}

	/**
	 * Build a multi-line action tooltip with modifier key descriptions.
	 * Format: "Base\n+ Shift : action\n+ Shift + Alt : action"
	 */
	inline FText MakeActionTooltip(const FText& Base, const TArray<TPair<FText, FText>>& Modifiers)
	{
		FString Result = Base.ToString();
		for (const TPair<FText, FText>& Mod : Modifiers)
		{
			Result += FString::Printf(TEXT("\n+ %s : %s"), *Mod.Key.ToString(), *Mod.Value.ToString());
		}
		return FText::FromString(Result);
	}

	/**
	 * Create a toggle button matching radio group visual style.
	 * Dark bg + white text when on, transparent + gray text when off.
	 */
	inline TSharedRef<SWidget> MakeToggleButton(
		const FText& Label,
		const FText& Tooltip,
		TFunction<bool()> IsOn,
		TFunction<void()> OnToggled)
	{
		return SNew(SButton)
			.ToolTipText(Tooltip)
			.ButtonColorAndOpacity_Lambda(
				[IsOn]
				{
					return IsOn() ? FLinearColor(0.005f, 0.005f, 0.005f, 0.8f) : FLinearColor::Transparent;
				})
			.OnClicked_Lambda(
				[OnToggled]()
				{
					OnToggled();
					return FReply::Handled();
				})
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Style::Label())
				.ColorAndOpacity_Lambda(
					[IsOn]
					{
						return IsOn() ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor::Gray);
					})
			];
	}

	/**
	 * Read ToolTip text from a UPROPERTY's metadata via reflection.
	 * Returns empty text if property or tooltip not found.
	 */
	inline FText GetPropertyTooltip(const UStruct* Struct, const FName& PropertyName)
	{
		if (Struct)
		{
			if (const FProperty* Prop = Struct->FindPropertyByName(PropertyName))
			{
				const FString* Tip = Prop->FindMetaData(TEXT("ToolTip"));
				if (Tip && !Tip->IsEmpty())
				{
					return FText::FromString(*Tip);
				}
			}
		}
		return FText::GetEmpty();
	}

	/** Get icon text for a connector type, resolving auto-assign via ConnectorSet. */
	inline FText GetConnectorIconText(const UPCGExValencyConnectorSet* Set, int32 TypeArrayIndex)
	{
		const int32 Effective = Set ? Set->GetEffectiveIconIndex(TypeArrayIndex) : TypeArrayIndex;
		const TCHAR C = PCGExValencyConnector::GetIconChar(Effective);
		return FText::FromString(FString(1, &C));
	}
}
