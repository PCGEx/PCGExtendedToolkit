// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExAssetEntryCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

void FPCGExAssetEntryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> WeightHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Weight));
	TSharedPtr<IPropertyHandle> CategoryHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category));
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, bIsSubCollection));

	HeaderRow.NameContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(2, 10)
			[
				GetAssetPicker(PropertyHandle, IsSubCollectionHandle)
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[

				SNew(SHorizontalBox)

				// Weight
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

				// Category
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("·· Category"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.MinWidth(50)
				.Padding(2, 0)
				[
					CategoryHandle->CreatePropertyValueWidget()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				// Wrap in a border to control opacity based on value
				SNew(SBorder)
				.BorderImage(FStyleDefaults::GetNoBrush())
				.ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						IsSubCollectionHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Sub-collection"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(8)
					]
				]
			]
		];
}

void FPCGExAssetEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumElements = 0;
	PropertyHandle->GetNumChildren(NumElements);

	for (uint32 i = 0; i < NumElements; ++i)
	{
		TSharedPtr<IPropertyHandle> ElementHandle = PropertyHandle->GetChildHandle(i);
		FName ElementName = ElementHandle ? ElementHandle->GetProperty()->GetFName() : NAME_None;
		if (!ElementHandle.IsValid() || CustomizedTopLevelProperties.Contains(ElementName)) { continue; }

		IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
		// Bind visibility dynamically
		PropertyRow.Visibility(
			MakeAttributeLambda(
				[ElementName]()
				{
					return GetDefault<UPCGExCollectionsEditorSettings>()->GetPropertyVisibility(ElementName);
				}));
	}

	// Add PropertyOverrides WITHOUT any visibility filter or customization
	// The visibility lambda interferes with nested customizations - prevents value widgets from rendering
	// PCGExPropertiesEditor module handles all PropertyOverrides UI via registered customizations
	TSharedPtr<IPropertyHandle> PropertyOverridesHandle = PropertyHandle->GetChildHandle(TEXT("PropertyOverrides"));
	if (PropertyOverridesHandle.IsValid())
	{
		ChildBuilder.AddProperty(PropertyOverridesHandle.ToSharedRef());
	}
}

void FPCGExAssetEntryCustomization::FillCustomizedTopLevelPropertiesNames()
{
	CustomizedTopLevelProperties.Add(FName("Weight"));
	CustomizedTopLevelProperties.Add(FName("Category"));
	CustomizedTopLevelProperties.Add(FName("bIsSubCollection"));
	CustomizedTopLevelProperties.Add(FName("SubCollection"));
	CustomizedTopLevelProperties.Add(FName("PropertyOverrides")); // Handled separately - no visibility filter
}

#define PCGEX_SUBCOLLECTION_VISIBLE \
.Visibility_Lambda([IsSubCollectionHandle](){ \
bool bWantsSubcollections = false; \
IsSubCollectionHandle->GetValue(bWantsSubcollections);\
return bWantsSubcollections ? EVisibility::Visible : EVisibility::Collapsed;})

#define PCGEX_SUBCOLLECTION_COLLAPSED \
.Visibility_Lambda([IsSubCollectionHandle](){ \
bool bWantsSubcollections = false; \
IsSubCollectionHandle->GetValue(bWantsSubcollections); \
return bWantsSubcollections ? EVisibility::Collapsed : EVisibility::Visible;})

#define PCGEX_ENTRY_INDEX \
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0)[\
SNew(STextBlock).Text_Lambda([PropertyHandle](){\
const int32 Index = PropertyHandle->GetIndexInArray();\
if (Index == INDEX_NONE) { return FText::FromString(TEXT("")); }\
return FText::FromString(FString::Printf(TEXT("%d →"), Index));}).Font(IDetailLayoutBuilder::GetDetailFont())\
.ColorAndOpacity(FSlateColor(FLinearColor(1,1,1,0.25)))]

void FPCGExEntryHeaderCustomizationBase::FillCustomizedTopLevelPropertiesNames()
{
	FPCGExAssetEntryCustomization::FillCustomizedTopLevelPropertiesNames();
	CustomizedTopLevelProperties.Add(GetAssetName());
}

TSharedRef<SWidget> FPCGExEntryHeaderCustomizationBase::GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
{
	TSharedPtr<IPropertyHandle> SubCollection = PropertyHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> AssetHandle = PropertyHandle->GetChildHandle(GetAssetName());

	return SNew(SHorizontalBox)
			PCGEX_ENTRY_INDEX
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.MinWidth(200)
			.Padding(2, 0)
			[
				SNew(SBox)
				PCGEX_SUBCOLLECTION_VISIBLE
				[
					SubCollection->CreatePropertyValueWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.MinWidth(200)
			.Padding(2, 0)
			[
				SNew(SBox)
				PCGEX_SUBCOLLECTION_COLLAPSED
				[
					AssetHandle->CreatePropertyValueWidget()
				]
			];
}

#define PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_IMPL(_CLASS, _NAME) \
TSharedRef<IPropertyTypeCustomization> FPCGEx##_CLASS##EntryCustomization::MakeInstance()\
{\
	TSharedRef<IPropertyTypeCustomization> Ref = MakeShareable(new FPCGEx##_CLASS##EntryCustomization());\
	static_cast<FPCGEx##_CLASS##EntryCustomization&>(Ref.Get()).FillCustomizedTopLevelPropertiesNames();\
	return Ref;\
}

PCGEX_FOREACH_ENTRY_TYPE(PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_IMPL)

#undef PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_DECL

#undef PCGEX_SUBCOLLECTION_VISIBLE
#undef PCGEX_SUBCOLLECTION_COLLAPSED
