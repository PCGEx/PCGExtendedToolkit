// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/InputSettings/PCGExInputShorthandsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Details/PCGExCustomizationMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Styling/AppStyle.h"
#include "UObject/TextProperty.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExInputShorthandsCustomization
{
	// One icon+label row inside the options popover; same active/inactive color language as the
	// inline ActionIcon buttons (dark plate + white icon when active, transparent + gray otherwise).
	// TextGlyph takes precedence over IconName (e.g. the 🆑 marker shared with node titles).
	TSharedRef<SWidget> CreateOptionRow(
		const FText& Label, const FText& Tooltip, const FString& IconName,
		TFunction<bool()> IsActive, TFunction<void()> OnClick, TAttribute<bool> Enabled = true,
		const FText& TextGlyph = FText::GetEmpty())
	{
		TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);

		if (!TextGlyph.IsEmpty())
		{
			Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(TextGlyph)
				// Color emoji glyphs ignore color tint; alpha is the only channel that dims them.
				.ColorAndOpacity_Lambda([IsActive] { return FSlateColor(IsActive() ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.3f)); })
			];
		}
		else if (!IconName.IsEmpty())
		{
			Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(*IconName))
				.ColorAndOpacity_Lambda([IsActive] { return IsActive() ? FLinearColor::White : FLinearColor::Gray; })
			];
		}

		Content->AddSlot().FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(Label)
		];

		return SNew(SButton)
			.ToolTipText(Tooltip)
			.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
			.IsEnabled(Enabled)
			.ButtonColorAndOpacity_Lambda([IsActive] { return IsActive() ? FLinearColor(0.005f, 0.005f, 0.005f, 0.8f) : FLinearColor::Transparent; })
			.OnClicked_Lambda(
				[OnClick]()
				{
					OnClick();
					return FReply::Handled();
				})
			[
				Content
			];
	}

	// Gear popover collapsing the shorthand's per-row options (Input mode + bCleanupAttribute) into
	// a single trailing button, instead of three inline ActionIcon buttons per row.
	TSharedRef<SWidget> CreateOptionsPopover(const TSharedPtr<IPropertyHandle>& InputHandle, const TSharedPtr<IPropertyHandle>& CleanupHandle)
	{
		TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);

		// Input mode rows, driven by the enum's own ActionIcon metadata (same source as the old radio group).
		if (UEnum* Enum = FindFirstObjectSafe<UEnum>(TEXT("EPCGExInputValueType")))
		{
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				if (Enum->HasMetaData(TEXT("Hidden"), i))
				{
					continue;
				}

				const FString KeyName = Enum->GetNameStringByIndex(i);
				FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);
				if (!IconName.IsEmpty())
				{
					IconName = TEXT("PCGEx.ActionIcon.") + IconName;
				}

				Menu->AddSlot().AutoHeight().Padding(2, 1)
				[
					CreateOptionRow(
						Enum->GetDisplayNameTextByIndex(i), Enum->GetToolTipTextByIndex(i), IconName,
						[InputHandle, KeyName]()
						{
							FString CurrentValue;
							InputHandle->GetValueAsFormattedString(CurrentValue);
							return CurrentValue == KeyName;
						},
						[InputHandle, KeyName]()
						{
							InputHandle->SetValueFromFormattedString(KeyName);
						})
				];
			}
		}

		Menu->AddSlot().AutoHeight().Padding(2, 4)
		[
			SNew(SSeparator)
		];

		Menu->AddSlot().AutoHeight().Padding(2, 1)
		[
			CreateOptionRow(
				CleanupHandle->GetPropertyDisplayName(), CleanupHandle->GetToolTipText(), FString(),
				[CleanupHandle]()
				{
					bool bValue = false;
					CleanupHandle->GetValue(bValue);
					return bValue;
				},
				[CleanupHandle]()
				{
					bool bValue = false;
					CleanupHandle->GetValue(bValue);
					CleanupHandle->SetValue(!bValue);
				},
				// EditCondition parity: only meaningful in Attribute mode.
				MakeAttributeLambda(
					[InputHandle]()
					{
						uint8 V = 0;
						InputHandle->GetValue(V);
						return V != 0;
					}),
				// Same marker the node title shows when consumable cleanup is active.
				FText::FromString(TEXT("🆑")))
		];

		return SNew(SComboButton)
			.HasDownArrow(false)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PCGEx.ActionIcon"))
			.ContentPadding(0)
			.ToolTipText(NSLOCTEXT("PCGExInputShorthands", "ShorthandOptions", "Input options"))
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("PCGEx.ActionIcon.Settings"))
				.DesiredSizeOverride(FVector2D(16.f, 16.f))
				.ColorAndOpacity(FLinearColor::White)
			]
			.MenuContent()
			[
				SNew(SBox)
				.Padding(4)
				.MinDesiredWidth(140)
				[
					Menu
				]
			];
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExInputShorthandCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExInputShorthandCustomization());
}

void FPCGExInputShorthandCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Get handles
	TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(FName("Input"));
	TSharedPtr<IPropertyHandle> ConstantHandle = PropertyHandle->GetChildHandle(FName("Constant"));
	TSharedPtr<IPropertyHandle> AttributeHandle = PropertyHandle->GetChildHandle(FName("Attribute"));
	TSharedPtr<IPropertyHandle> CleanupHandle = PropertyHandle->GetChildHandle(FName("bCleanupAttribute"));

	HeaderRow.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Collapsed : EVisibility::Visible;
						}))
				[
					CreateValueWidget(ConstantHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Visible : EVisibility::Collapsed;
						}))
				[
					CreateAttributeWidget(AttributeHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1).AutoWidth().VAlign(VAlign_Center)
			[
				PCGExInputShorthandsCustomization::CreateOptionsPopover(InputHandle, CleanupHandle)
			]
		];
}

void FPCGExInputShorthandCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

TSharedRef<SWidget> FPCGExInputShorthandCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	return ValueHandle->CreatePropertyValueWidget();
}

TSharedRef<SWidget> FPCGExInputShorthandCustomization::CreateAttributeWidget(TSharedPtr<IPropertyHandle> AttributeHandle)
{
	FProperty* Prop = AttributeHandle->GetProperty();
	if (CastField<FNameProperty>(Prop) || CastField<FTextProperty>(Prop))
	{
		return AttributeHandle->CreatePropertyValueWidget();
	}

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.MaxDesiredHeight(22.0f)
		[
			SNew(SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda(
				[AttributeHandle]()
				{
					TArray<void*> RawData;
					AttributeHandle->AccessRawData(RawData);

					if (RawData.Num() > 0)
					{
						FPCGAttributePropertyInputSelector* Selector = static_cast<FPCGAttributePropertyInputSelector*>(RawData[0]);
						if (Selector)
						{
							return FText::FromString(Selector->ToString());
						}
					}
					return FText::GetEmpty();
				})
			.OnTextCommitted_Lambda(
				[AttributeHandle](const FText& NewText, ETextCommit::Type CommitType)
				{
					// Only handle commits from Enter or losing focus
					if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
					{
						TArray<void*> RawData;
						AttributeHandle->AccessRawData(RawData);

						bool bUpdated = false;
						for (void* Ptr : RawData)
						{
							FPCGAttributePropertyInputSelector* Selector = static_cast<FPCGAttributePropertyInputSelector*>(Ptr);
							if (Selector)
							{
								Selector->Update(NewText.ToString());
								bUpdated = true;
							}
						}

						if (bUpdated)
						{
							AttributeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
						}
					}
				})
		];
}

TSharedRef<IPropertyTypeCustomization> FPCGExInputShorthandVectorCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExInputShorthandVectorCustomization());
}

TSharedRef<SWidget> FPCGExInputShorthandVectorCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	return PCGEX_VECTORINPUTBOX(ValueHandle);
}

TSharedRef<IPropertyTypeCustomization> FPCGExInputShorthandDirectionCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExInputShorthandDirectionCustomization());
}

void FPCGExInputShorthandDirectionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Get handles
	TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(FName("Input"));
	TSharedPtr<IPropertyHandle> ConstantHandle = PropertyHandle->GetChildHandle(FName("Constant"));
	TSharedPtr<IPropertyHandle> AttributeHandle = PropertyHandle->GetChildHandle(FName("Attribute"));
	TSharedPtr<IPropertyHandle> FlipHandle = PropertyHandle->GetChildHandle(FName("bFlip"));
	TSharedPtr<IPropertyHandle> CleanupHandle = PropertyHandle->GetChildHandle(FName("bCleanupAttribute"));

	HeaderRow.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).AutoWidth().VAlign(VAlign_Center)
			[
				PCGExInputShorthandsCustomization::CreateOptionsPopover(InputHandle, CleanupHandle)
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Collapsed : EVisibility::Visible;
						}))
				[
					CreateValueWidget(ConstantHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Visible : EVisibility::Collapsed;
						}))
				[
					CreateAttributeWidget(AttributeHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				FlipHandle->CreatePropertyValueWidget()
			]
		];
}

TSharedRef<IPropertyTypeCustomization> FPCGExInputShorthandRotatorCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExInputShorthandRotatorCustomization());
}

TSharedRef<SWidget> FPCGExInputShorthandRotatorCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	return PCGEX_ROTATORINPUTBOX(ValueHandle);
}

TSharedRef<IPropertyTypeCustomization> FPCGExInputShorthandSoftObjectPathCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExInputShorthandSoftObjectPathCustomization());
}

TSharedRef<SWidget> FPCGExInputShorthandSoftObjectPathCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	// When the DECLARING UPROPERTY names an allowed type via the standard AllowedClasses meta
	// (e.g. meta=(AllowedClasses="/Script/PCGExCollections.PCGExVariantCollection")), render a
	// strongly-typed asset picker — same approach as PCGExProperties' soft-path widgets.
	// GetOwnerProperty() resolves the declaring property in both the plain-member case (self)
	// and the array-element case (UHT stores the meta on the FArrayProperty).
	UClass* AllowedClass = nullptr;
	if (const TSharedPtr<IPropertyHandle> StructHandle = ValueHandle->GetParentHandle())
	{
		const FProperty* Property = StructHandle->GetProperty();
		if (const FProperty* OwnerProperty = Property ? Property->GetOwnerProperty() : nullptr)
		{
			FString Meta = OwnerProperty->GetMetaData(TEXT("AllowedClasses"));
			if (!Meta.IsEmpty())
			{
				// Single-type picker: first entry wins when a list is provided.
				FString First = MoveTemp(Meta);
				if (int32 CommaIdx; First.FindChar(TEXT(','), CommaIdx))
				{
					First.LeftInline(CommaIdx);
				}
				First.TrimStartAndEndInline();

				AllowedClass = FSoftClassPath(First).ResolveClass();
				if (!AllowedClass)
				{
					AllowedClass = FSoftClassPath(First).TryLoadClass<UObject>();
				}
			}
		}
	}

	if (AllowedClass)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(SObjectPropertyEntryBox)
				.PropertyHandle(ValueHandle)
				.AllowedClass(AllowedClass)
				.AllowClear(true)
				.DisplayThumbnail(false)
			];
	}

	// FSoftObjectPath's default property widget renders as a multi-row asset picker that doesn't
	// fit our header layout, and an asset picker isn't meaningful here -- the path is resolved at
	// graph execution against live actors. A plain editable text box matches the attribute fallback.
	return SNew(SBox)
		.VAlign(VAlign_Center)
		.MaxDesiredHeight(22.0f)
		[
			SNew(SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda(
				[ValueHandle]()
				{
					TArray<void*> RawData;
					ValueHandle->AccessRawData(RawData);
					if (RawData.Num() > 0)
					{
						if (const FSoftObjectPath* Path = static_cast<const FSoftObjectPath*>(RawData[0]))
						{
							return FText::FromString(Path->ToString());
						}
					}
					return FText::GetEmpty();
				})
			.OnTextCommitted_Lambda(
				[ValueHandle](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
					{
						return;
					}

					ValueHandle->NotifyPreChange();

					TArray<void*> RawData;
					ValueHandle->AccessRawData(RawData);

					const FString NewPath = NewText.ToString();
					bool bUpdated = false;
					for (void* Ptr : RawData)
					{
						if (FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(Ptr))
						{
							Path->SetPath(NewPath);
							bUpdated = true;
						}
					}

					if (bUpdated)
					{
						ValueHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
					}
				})
		];
}
