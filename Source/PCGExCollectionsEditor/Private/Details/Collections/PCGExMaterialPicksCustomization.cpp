// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExMaterialPicksCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Collections/PCGExMeshCollection.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#pragma region FPCGExMaterialOverrideSingleEntryCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExMaterialOverrideSingleEntryCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExMaterialOverrideSingleEntryCustomization());
}

void FPCGExMaterialOverrideSingleEntryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> WeightHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideSingleEntry, Weight));
	TSharedPtr<IPropertyHandle> MaterialHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideSingleEntry, Material));


	HeaderRow.NameContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Weight       "))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2, 0)
			[
				WeightHandle->CreatePropertyValueWidget()
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			MaterialHandle->CreatePropertyValueWidget()
		];
}

void FPCGExMaterialOverrideSingleEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#pragma endregion

#pragma region FPCGExMaterialOverrideEntryCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExMaterialOverrideEntryCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExMaterialOverrideEntryCustomization());
}

void FPCGExMaterialOverrideEntryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> SlotIndexHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideEntry, SlotIndex));
	TSharedPtr<IPropertyHandle> MaterialHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideEntry, Material));

	HeaderRow.NameContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Slot Index")))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(FText::FromString(TEXT("Using -1 will use the index inside this array.")))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				.MinDesiredWidth(10)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.MinWidth(50)
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					// Wrap in a border to control opacity based on value
					SNew(SBorder)
					.BorderImage(FStyleDefaults::GetNoBrush())
					.ColorAndOpacity_Lambda(
						[SlotIndexHandle]()
						{
							int32 V = -1;
							SlotIndexHandle->GetValue(V);
							return FLinearColor(1, 1, 1, V < 0 ? 0.6f : 1.0f);
						})
					[
						SlotIndexHandle->CreatePropertyValueWidget()
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0)\
				[
					SNew(STextBlock)
					.Text_Lambda(
						[PropertyHandle, SlotIndexHandle]()
						{
							int32 V = -1;
							SlotIndexHandle->GetValue(V);
							const int32 Index = PropertyHandle->GetIndexInArray();
							if (Index == INDEX_NONE || V >= 0)
							{
								return FText::FromString(TEXT(""));
							}
							return FText::FromString(FString::Printf(TEXT("→ %d"), Index));
						})
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.25)))
				]
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			MaterialHandle->CreatePropertyValueWidget()
		];
}

void FPCGExMaterialOverrideEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#pragma endregion

#pragma region FPCGExMaterialOverrideCollectionCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExMaterialOverrideCollectionCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExMaterialOverrideCollectionCustomization());
}

void FPCGExMaterialOverrideCollectionCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> WeightHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideCollection, Weight));
	TSharedPtr<IPropertyHandle> OverridesHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideCollection, Overrides));


	HeaderRow.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Weight"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.MinWidth(50)
			.Padding(2, 0)
			[
				WeightHandle->CreatePropertyValueWidget()
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			OverridesHandle->CreatePropertyValueWidget()
		];
}

void FPCGExMaterialOverrideCollectionCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> OverridesHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExMaterialOverrideCollection, Overrides));

	// Rebuild the entry rows when the inner array gains/loses elements -- otherwise the
	// header counter ticks up while these child rows stay a stale build-time snapshot.
	const TWeakPtr<IPropertyUtilities> WeakUtils = CustomizationUtils.GetPropertyUtilities();
	OverridesHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtils]()
	{
		if (const TSharedPtr<IPropertyUtilities> Utils = WeakUtils.Pin()) { Utils->ForceRefresh(); }
	}));

	uint32 NumChildren = 0;
	OverridesHandle->GetNumChildren(NumChildren);

	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		TSharedPtr<IPropertyHandle> Child = OverridesHandle->GetChildHandle(Index);

		ChildBuilder.AddProperty(Child.ToSharedRef())
		            .ShouldAutoExpand(true);
	}
}

#pragma endregion
