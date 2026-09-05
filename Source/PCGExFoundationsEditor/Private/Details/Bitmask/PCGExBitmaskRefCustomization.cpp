// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Bitmask/PCGExBitmaskRefCustomization.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Data/Bitmasks/PCGExBitmaskCollection.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExBitmaskRefCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExBitmaskRefCustomization());
}

void FPCGExBitmaskRefCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	SourceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskRef, Source));
	IdentifierHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskRef, Identifier));
	TSharedPtr<IPropertyHandle> OperationHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskRef, Op));


	HeaderRow
		.NameContent()
		.MinDesiredWidth(50)
		[
			SNew(SObjectPropertyEntryBox)
			.PropertyHandle(SourceHandle)
			.AllowedClass(UPCGExBitmaskCollection::StaticClass())
			.DisplayThumbnail(true)
			.AllowClear(true)
			.ThumbnailSizeOverride(FIntPoint(24, 24))
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.MinWidth(50)
			.VAlign(VAlign_Center)
			.Padding(2, 2)
			[
				OperationHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			.Padding(2, 2)
			[
				SAssignNew(ComboBoxWidget, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&ComboOptions)
				.OnGenerateWidget_Lambda(
					[](TSharedPtr<FName> InItem)
					{
						return SNew(STextBlock).Text(FText::FromName(*InItem));
					})
				.OnSelectionChanged_Lambda(
					[this](TSharedPtr<FName> NewValue, ESelectInfo::Type)
					{
						if (NewValue.IsValid())
						{
							IdentifierHandle->SetValue(*NewValue);
						}
					})
				[
					SNew(STextBlock)
					.Text_Lambda(
						[this]()
						{
							FName CurrentValue;
							IdentifierHandle->GetValue(CurrentValue);
							return FText::FromName(CurrentValue);
						})
				]
			]
		];

	RefreshOptions();

	// Optional: Register callback when Source changes
	SourceHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExBitmaskRefCustomization::RefreshOptions));
}

void FPCGExBitmaskRefCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FPCGExBitmaskRefCustomization::RefreshOptions()
{
	ComboOptions.Empty();

	bool bNoOptions = true;
	UObject* SourceObject = nullptr;
	if (SourceHandle->GetValue(SourceObject) == FPropertyAccess::Success)
	{
		if (UPCGExBitmaskCollection* Collection = Cast<UPCGExBitmaskCollection>(SourceObject))
		{
			TArray<FName> Identifiers = Collection->EDITOR_GetIdentifierOptions();
			for (const FName& Name : Identifiers)
			{
				ComboOptions.Add(MakeShared<FName>(Name));
			}
			bNoOptions = Identifiers.IsEmpty();
		}
	}

	if (bNoOptions)
	{
		ComboOptions.Add(MakeShared<FName>(FName("{Empty}")));
	}

	if (ComboBoxWidget.IsValid())
	{
		ComboBoxWidget->RefreshOptions();

		// Set initially selected item based on current Identifier value
		FName CurrentValue;
		if (IdentifierHandle->GetValue(CurrentValue) == FPropertyAccess::Success)
		{
			for (auto& Option : ComboOptions)
			{
				if (*Option == CurrentValue)
				{
					ComboBoxWidget->SetSelectedItem(Option);
					break;
				}
			}
		}
	}
}
