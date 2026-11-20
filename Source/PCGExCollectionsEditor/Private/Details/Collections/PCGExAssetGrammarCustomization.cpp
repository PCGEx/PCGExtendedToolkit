// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExAssetGrammarCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetGrammar.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExAssetGrammarCustomization
{
	// Manual color picker plumbing. The default CreatePropertyValueWidget() for an FLinearColor
	// nested inside FPCGExAssetGrammarDetails inside an entry struct inside the collection's
	// Entries array doesn't propagate writes back to the collection -- picking a color updates
	// nothing and leaves the package clean. Driving the picker manually and calling
	// NotifyFinishedChangingProperties() forces PostEditChangeProperty + MarkPackageDirty
	// the same way FColorStructCustomization does it for top-level FLinearColor properties.
	TSharedRef<SWidget> MakeColorWidget(TSharedPtr<IPropertyHandle> Handle)
	{
		return SNew(SColorBlock)
			.Color_Lambda([Handle]() -> FLinearColor
			{
				FLinearColor C = FLinearColor::White;
				if (Handle.IsValid())
				{
					void* Data = nullptr;
					if (Handle->GetValueData(Data) == FPropertyAccess::Success && Data)
					{
						C = *static_cast<const FLinearColor*>(Data);
					}
				}
				return C;
			})
			.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
			.ShowBackgroundForAlpha(false)
			.Size(FVector2D(22.f, 18.f))
			.OnMouseButtonDown_Lambda([Handle](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
			{
				if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
				if (!Handle.IsValid()) { return FReply::Unhandled(); }

				FLinearColor Initial = FLinearColor::White;
				void* Data = nullptr;
				if (Handle->GetValueData(Data) == FPropertyAccess::Success && Data)
				{
					Initial = *static_cast<const FLinearColor*>(Data);
				}

				FColorPickerArgs PickerArgs;
				PickerArgs.bUseAlpha = false;
				PickerArgs.bOnlyRefreshOnMouseUp = false;
				PickerArgs.bOnlyRefreshOnOk = false;
				PickerArgs.InitialColor = Initial;
				PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
					[Handle](FLinearColor NewColor)
					{
						if (!Handle.IsValid()) { return; }
						Handle->SetValueFromFormattedString(NewColor.ToString());
						Handle->NotifyFinishedChangingProperties();
					});

				OpenColorPicker(PickerArgs);
				return FReply::Handled();
			});
	}

	/**
	 * Determine whether the enclosing entry is a subcollection. Used to gate which
	 * EPCGExGrammarAxisSize values are surfaced in the per-axis Size selector.
	 *
	 *   AssetGrammar lives on an entry          -> read entry's bIsSubCollection
	 *   GlobalAssetGrammar lives on a collection -> always leaf context (applies to leaf entries on overrule)
	 *   SubCollectionGrammar lives on a collection -> always subcollection context (this collection acts as a sub)
	 *
	 * Snapshotted at customization construction; toggling bIsSubCollection mid-edit may require
	 * collapsing/expanding the row to refresh the available size modes.
	 */
	bool DetermineSubCollectionContext(TSharedRef<IPropertyHandle> PropertyHandle)
	{
		if (FProperty* Prop = PropertyHandle->GetProperty())
		{
			const FName PropName = Prop->GetFName();
			if (PropName == FName(TEXT("SubCollectionGrammar"))) { return true; }
			if (PropName == FName(TEXT("GlobalAssetGrammar")))   { return false; }
		}
		// Entry-level AssetGrammar -- read bIsSubCollection from the entry struct (the parent handle).
		if (TSharedPtr<IPropertyHandle> EntryHandle = PropertyHandle->GetParentHandle())
		{
			if (TSharedPtr<IPropertyHandle> bIsSubHandle = EntryHandle->GetChildHandle(FName(TEXT("bIsSubCollection"))))
			{
				bool bIsSub = false;
				if (bIsSubHandle->GetValue(bIsSub) == FPropertyAccess::Success)
				{
					return bIsSub;
				}
			}
		}
		return false;
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExAssetGrammarCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExAssetGrammarCustomization());
}

void FPCGExAssetGrammarCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> SymbolHandle     = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, Symbol));
	TSharedPtr<IPropertyHandle> DebugColorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, DebugColor));
	TSharedPtr<IPropertyHandle> AxesHandle       = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, Axes));

	// GrammarSource lives on the surrounding entry. When = Global (or collection-level Overrule),
	// the entry's own AssetGrammar is ignored at runtime -- we mirror that by graying its controls.
	TSharedPtr<IPropertyHandle> GrammarSourceHandle = nullptr;
	if (TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle())
	{
		GrammarSourceHandle = ParentHandle->GetChildHandle(FName("GrammarSource"));
	}

	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	// "Global" detection drives the disabled-state hint on the entry-level grammar (the entry struct
	// loses authoring power when the collection overrules globally) and on the GlobalAssetGrammar
	// itself (always editable -- it IS the global source).
	const bool bIsGlobal = PropertyHandle->GetProperty()
		? PropertyHandle->GetProperty()->GetFName().ToString().Contains(TEXT("Global"))
		: false;

	UPCGExAssetCollection* Collection = !OuterObjects.IsEmpty() ? Cast<UPCGExAssetCollection>(OuterObjects[0]) : nullptr;

	auto IsLocalData = [GrammarSourceHandle, bIsGlobal]()
	{
		if (bIsGlobal || !GrammarSourceHandle) { return true; }
		uint8 EnumValue = 0;
		GrammarSourceHandle->GetValue(EnumValue);
		return !EnumValue; // EPCGExEntryVariationMode::Local == 0
	};

	// NameContent: property name | Axes checkbox group | (italic Overrule hint when applicable).
	// Axes lives on the left as the entry's primary "what does this module do" toggle. The
	// label is dropped -- the X/Y/Z icons speak for themselves.
	TSharedRef<SHorizontalBox> NameBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(1).AutoWidth().VAlign(VAlign_Center)
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		+ SHorizontalBox::Slot().Padding(6, 0, 1, 0).AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).IsEnabled_Lambda(IsLocalData)
			[
				PCGExEnumCustomization::CreateCheckboxGroup(AxesHandle, TEXT("EPCGExGrammarAxes"), {})
			]
		];

	if (Collection && !bIsGlobal)
	{
		NameBox->AddSlot().Padding(6, 0, 1, 0).FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.Text_Lambda([Collection]()
			{
				return Collection->GlobalGrammarMode == EPCGExGlobalVariationRule::Overrule
					? FText::FromString(TEXT("··· Overruled"))
					: FText::GetEmpty();
			})
			.ColorAndOpacity_Lambda([Collection]()
			{
				return Collection->GlobalGrammarMode == EPCGExGlobalVariationRule::Overrule
					? FLinearColor(1.0f, 0.5f, 0.1f, 0.5)
					: FLinearColor::Transparent;
			})
		];
	}

	HeaderRow.NameContent().MinDesiredWidth(220)[NameBox];

	// ValueContent: Symbol input (fills) | DebugColor swatch pinned right.
	HeaderRow.ValueContent()
	         .MinDesiredWidth(220)
	[
		SNew(SHorizontalBox)

		// Symbol -- fills available width.
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(SBox).IsEnabled_Lambda([bIsGlobal]() { return !bIsGlobal; })
			[
				SymbolHandle->CreatePropertyValueWidget()
			]
		]

		// DebugColor swatch (manual picker -- standard handle write doesn't propagate from nested struct in array entry).
		+ SHorizontalBox::Slot().Padding(6, 0, 1, 0).AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).IsEnabled_Lambda(IsLocalData)
			[
				PCGExAssetGrammarCustomization::MakeColorWidget(DebugColorHandle)
			]
		]
	];
}

namespace PCGExAssetGrammarCustomization
{
	/** Matrix-shaped popup picker for EPCGExGrammarAxisSize (Fixed row + 3 aggregators x 3 axes).
	 *  Combo button shows the current selection's display name; clicking a cell commits and closes. */
	TSharedRef<SWidget> CreateSizeMatrixDropdown(TSharedPtr<IPropertyHandle> SizeHandle)
	{
		UEnum* Enum = FindFirstObjectSafe<UEnum>(TEXT("EPCGExGrammarAxisSize"));
		if (!Enum || !SizeHandle.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		// Shared holder so cell click handlers can close the popup after committing.
		TSharedRef<TWeakPtr<SComboButton>> ComboHolder = MakeShared<TWeakPtr<SComboButton>>();

		auto MakeCell = [SizeHandle, Enum, ComboHolder](EPCGExGrammarAxisSize CellVal, const FText& Label, float MinWidth) -> TSharedRef<SWidget>
		{
			const uint8 CellU8 = static_cast<uint8>(CellVal);
			const int32 Idx = Enum->GetIndexByValue(CellU8);
			const FText ToolTip = (Idx != INDEX_NONE) ? Enum->GetToolTipTextByIndex(Idx) : FText::GetEmpty();

			auto IsSelected = [SizeHandle, CellU8]() -> bool
			{
				uint8 V = 0;
				return SizeHandle->GetValue(V) == FPropertyAccess::Success && V == CellU8;
			};

			return SNew(SBox).MinDesiredWidth(MinWidth).MinDesiredHeight(22)
			[
				SNew(SButton)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ContentPadding(FMargin(4, 1))
				.ToolTipText(ToolTip)
				.ButtonColorAndOpacity_Lambda([IsSelected]() -> FSlateColor
				{
					return IsSelected() ? FLinearColor(0.005f, 0.005f, 0.005f, 0.85f) : FLinearColor::Transparent;
				})
				.OnClicked_Lambda([SizeHandle, CellU8, ComboHolder]()
				{
					SizeHandle->SetValue(CellU8);
					if (TSharedPtr<SComboButton> Pinned = ComboHolder->Pin())
					{
						Pinned->SetIsOpen(false);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.ColorAndOpacity_Lambda([IsSelected]() -> FSlateColor
					{
						return IsSelected() ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f);
					})
				]
			];
		};

		auto MakeRowLabel = [](const FText& Text) -> TSharedRef<SWidget>
		{
			return SNew(SBox).MinDesiredWidth(70).VAlign(VAlign_Center).Padding(FMargin(4, 0))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
			];
		};

		auto GetMenuContent = [MakeCell, MakeRowLabel]() -> TSharedRef<SWidget>
		{
			constexpr float AxisCellWidth = 28.f;
			constexpr float LabelColWidth = 70.f;
			// Fixed row matches label + 3 cells so the matrix stays rectangular.
			constexpr float FixedRowWidth = LabelColWidth + AxisCellWidth * 3.f + 4.f;

			return SNew(SBox).Padding(FMargin(4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(2))
				[
					MakeCell(EPCGExGrammarAxisSize::Fixed, FText::FromString(TEXT("Fixed")), FixedRowWidth)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ MakeRowLabel(FText::FromString(TEXT("Smallest"))) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Min_X, FText::FromString(TEXT("X")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Min_Y, FText::FromString(TEXT("Y")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Min_Z, FText::FromString(TEXT("Z")), AxisCellWidth) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ MakeRowLabel(FText::FromString(TEXT("Largest"))) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Max_X, FText::FromString(TEXT("X")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Max_Y, FText::FromString(TEXT("Y")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Max_Z, FText::FromString(TEXT("Z")), AxisCellWidth) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ MakeRowLabel(FText::FromString(TEXT("Average"))) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Avg_X, FText::FromString(TEXT("X")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Avg_Y, FText::FromString(TEXT("Y")), AxisCellWidth) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1, 0)[ MakeCell(EPCGExGrammarAxisSize::Avg_Z, FText::FromString(TEXT("Z")), AxisCellWidth) ]
				]
			];
		};

		TSharedPtr<SComboButton> Combo;
		SAssignNew(Combo, SComboButton)
			.HasDownArrow(true)
			.OnGetMenuContent_Lambda(GetMenuContent)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([SizeHandle, Enum]()
				{
					uint8 V = 0;
					if (SizeHandle->GetValue(V) != FPropertyAccess::Success) { return FText::GetEmpty(); }
					const int32 Idx = Enum->GetIndexByValue(V);
					return Idx != INDEX_NONE ? Enum->GetDisplayNameTextByIndex(Idx) : FText::GetEmpty();
				})
			];

		*ComboHolder = Combo;
		return Combo.ToSharedRef();
	}

	/** Build one per-axis row. Hidden when the matching Axes bit is unset.
	 *  Layout: NameContent = axis letter + Size radio + Scalable checkbox.
	 *          ValueContent = SizeOp cycle button (when not Fixed) + FixedSize spinner (when Fixed or op-set). */
	// Templated on the IsLocalData predicate type so we preserve the original lambda closure
	// type. Slate's IsEnabled_Lambda binds to a TFunction<bool()>&& rvalue; lambda closures
	// implicitly construct fresh temporaries (rvalues), but an lvalue TFunction parameter
	// would not bind. Template => each .IsEnabled_Lambda(IsLocalData) is a fresh conversion.
	template <typename TIsLocalData>
	void AddAxisRow(
		IDetailChildrenBuilder& ChildBuilder,
		TSharedPtr<IPropertyHandle> AxesHandle,
		TSharedPtr<IPropertyHandle> SizingHandle,
		const EPCGExGrammarAxes AxisBit,
		const FString& AxisLetter,
		TIsLocalData IsLocalData,
		const bool bIsSubContext)
	{
		if (!SizingHandle.IsValid() || !AxesHandle.IsValid()) { return; }

		TSharedPtr<IPropertyHandle> SizeHandle      = SizingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExGrammarAxisDetails, Size));
		TSharedPtr<IPropertyHandle> SizeOpHandle    = SizingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExGrammarAxisDetails, SizeOp));
		TSharedPtr<IPropertyHandle> FixedSizeHandle = SizingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExGrammarAxisDetails, FixedSize));
		TSharedPtr<IPropertyHandle> ScalableHandle  = SizingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExGrammarAxisDetails, bScalable));

		// Gate Size options by context: leaves can't aggregate (no children), subs have no own bounds.
		// EPCGExGrammarAxisSize: 0=Bounds, 1=Fixed, 2..4=Min_X/Y/Z, 5..7=Max_X/Y/Z, 8..10=Avg_X/Y/Z.
		const TSet<int32> SkipSizeIndices = bIsSubContext
			? TSet<int32>{0}                              // hide Bounds for subcollections
			: TSet<int32>{2, 3, 4, 5, 6, 7, 8, 9, 10};    // hide all Min_*/Max_*/Avg_* for leaves

		const uint8 BitMask = static_cast<uint8>(AxisBit);

		auto RowVisibility = [AxesHandle, BitMask]() -> EVisibility
		{
			uint8 Mask = 0;
			if (AxesHandle->GetValue(Mask) == FPropertyAccess::Success && (Mask & BitMask) != 0)
			{
				return EVisibility::Visible;
			}
			return EVisibility::Collapsed;
		};

		ChildBuilder.AddCustomRow(FText::FromString(AxisLetter))
			.Visibility(TAttribute<EVisibility>::CreateLambda(RowVisibility))
			.NameContent()
			[
				SNew(SHorizontalBox)
				// Axis letter -- the row's identity.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 6, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(AxisLetter))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.MinDesiredWidth(10)
				]
				// Leaf: inline icon radios for Bounds/Fixed. Subcollection: matrix popup for the 10 entries.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).IsEnabled_Lambda(IsLocalData)
					[
						bIsSubContext
							? PCGExAssetGrammarCustomization::CreateSizeMatrixDropdown(SizeHandle)
							: PCGExEnumCustomization::CreateRadioGroup(SizeHandle, TEXT("EPCGExGrammarAxisSize"), SkipSizeIndices)
					]
				]
			]
			.ValueContent()
			.MinDesiredWidth(220)
			[
				SNew(SHorizontalBox)
				// SizeOp cycle (hidden when Size == Fixed -- op makes no sense on a literal).
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(22)
					.IsEnabled_Lambda(IsLocalData)
					.Visibility_Lambda([SizeHandle]()
					{
						uint8 EnumValue = 0;
						if (SizeHandle->GetValue(EnumValue) == FPropertyAccess::Success)
						{
							return EnumValue == static_cast<uint8>(EPCGExGrammarAxisSize::Fixed)
								? EVisibility::Collapsed : EVisibility::Visible;
						}
						return EVisibility::Visible;
					})
					[
						PCGExEnumCustomization::CreateCycleButton(SizeOpHandle, TEXT("EPCGExGrammarSizeOp"))
					]
				]
				// FixedSize spinner. Visible when Size == Fixed (literal) OR SizeOp != None (offset/multiplier).
				+ SHorizontalBox::Slot().Padding(1).FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SBox)
					.IsEnabled_Lambda(IsLocalData)
					.Visibility_Lambda([SizeHandle, SizeOpHandle]()
					{
						uint8 SizeEnum = 0;
						if (SizeHandle->GetValue(SizeEnum) == FPropertyAccess::Success &&
							SizeEnum == static_cast<uint8>(EPCGExGrammarAxisSize::Fixed))
						{
							return EVisibility::Visible;
						}
						uint8 OpEnum = 0;
						if (SizeOpHandle->GetValue(OpEnum) == FPropertyAccess::Success &&
							OpEnum != static_cast<uint8>(EPCGExGrammarSizeOp::None))
						{
							return EVisibility::Visible;
						}
						return EVisibility::Collapsed;
					})
					[
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([FixedSizeHandle]() -> TOptional<double>
						{
							double V = 0;
							FixedSizeHandle->GetValue(V);
							return V;
						})
						.OnValueCommitted_Lambda([FixedSizeHandle](double NewVal, ETextCommit::Type)
						{
							FixedSizeHandle->SetValue(NewVal);
						})
						.ToolTipText(FixedSizeHandle->GetToolTipText())
						.AllowSpin(true)
						// Allow negative values only in Offset mode (multiplier 0..N, fixed >0).
						.MinValue_Lambda([SizeOpHandle]() -> TOptional<double>
						{
							uint8 OpEnum = 0;
							if (SizeOpHandle->GetValue(OpEnum) == FPropertyAccess::Success &&
								OpEnum == static_cast<uint8>(EPCGExGrammarSizeOp::Offset))
							{
								return TOptional<double>();
							}
							return 0.0;
						})
						.MinSliderValue_Lambda([SizeOpHandle]() -> TOptional<double>
						{
							uint8 OpEnum = 0;
							if (SizeOpHandle->GetValue(OpEnum) == FPropertyAccess::Success &&
								OpEnum == static_cast<uint8>(EPCGExGrammarSizeOp::Offset))
							{
								return -1000.0;
							}
							return 0.0;
						})
						.MaxSliderValue(1000.0)
					]
				]
				// Scalable (Flex) toggle on the right -- last in the row, pinned by AutoWidth.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 2, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Flex")))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(SBox).IsEnabled_Lambda(IsLocalData)
					[
						ScalableHandle->CreatePropertyValueWidget()
					]
				]
			];
	}
}

void FPCGExAssetGrammarCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> AxesHandle    = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, Axes));
	TSharedPtr<IPropertyHandle> SizingXHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, SizingX));
	TSharedPtr<IPropertyHandle> SizingYHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, SizingY));
	TSharedPtr<IPropertyHandle> SizingZHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetGrammarDetails, SizingZ));

	const bool bIsSubContext = PCGExAssetGrammarCustomization::DetermineSubCollectionContext(PropertyHandle);
	const bool bIsGlobal = PropertyHandle->GetProperty()
		? PropertyHandle->GetProperty()->GetFName().ToString().Contains(TEXT("Global"))
		: false;

	// Mirror IsLocalData from the header so per-axis rows gray out under Overrule too.
	TSharedPtr<IPropertyHandle> GrammarSourceHandle = nullptr;
	if (TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle())
	{
		GrammarSourceHandle = ParentHandle->GetChildHandle(FName("GrammarSource"));
	}
	auto IsLocalData = [GrammarSourceHandle, bIsGlobal]() -> bool
	{
		if (bIsGlobal || !GrammarSourceHandle) { return true; }
		uint8 EnumValue = 0;
		GrammarSourceHandle->GetValue(EnumValue);
		return !EnumValue;
	};

	PCGExAssetGrammarCustomization::AddAxisRow(ChildBuilder, AxesHandle, SizingXHandle, EPCGExGrammarAxes::X, TEXT("X"), IsLocalData, bIsSubContext);
	PCGExAssetGrammarCustomization::AddAxisRow(ChildBuilder, AxesHandle, SizingYHandle, EPCGExGrammarAxes::Y, TEXT("Y"), IsLocalData, bIsSubContext);
	PCGExAssetGrammarCustomization::AddAxisRow(ChildBuilder, AxesHandle, SizingZHandle, EPCGExGrammarAxes::Z, TEXT("Z"), IsLocalData, bIsSubContext);
}
