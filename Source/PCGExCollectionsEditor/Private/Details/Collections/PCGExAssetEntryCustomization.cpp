// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExAssetEntryCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Selection.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
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
					SNew(STextBlock).Text(FText::FromString(TEXT("Weight"))).ToolTipText(WeightHandle->GetToolTipText()).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.MinWidth(50)
				.Padding(2, 0)
				[
					SNew(SBox).ToolTipText(WeightHandle->GetToolTipText())
					[
						WeightHandle->CreatePropertyValueWidget()
					]
				]

				// Category
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("·· Category"))).ToolTipText(CategoryHandle->GetToolTipText()).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.MinWidth(50)
				.Padding(2, 0)
				[
					SNew(SBox).ToolTipText(CategoryHandle->GetToolTipText())
					[
						CategoryHandle->CreatePropertyValueWidget()
					]
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
				.ToolTipText(IsSubCollectionHandle->GetToolTipText())
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
						SNew(STextBlock).Text(FText::FromString(TEXT("Sub-collection"))).ToolTipText(IsSubCollectionHandle->GetToolTipText()).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(8)
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
				.ToolTipText(SubCollection->GetToolTipText())
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
				.ToolTipText(AssetHandle->GetToolTipText())
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

#undef PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_IMPL

#pragma region FPCGExActorEntryCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExActorEntryCustomization::MakeInstance()
{
	TSharedRef<IPropertyTypeCustomization> Ref = MakeShareable(new FPCGExActorEntryCustomization());
	static_cast<FPCGExActorEntryCustomization&>(Ref.Get()).FillCustomizedTopLevelPropertiesNames();
	return Ref;
}

void FPCGExActorEntryCustomization::FillCustomizedTopLevelPropertiesNames()
{
	FPCGExEntryHeaderCustomizationBase::FillCustomizedTopLevelPropertiesNames();
	CustomizedTopLevelProperties.Add(FName("DeltaSourceLevel"));
	CustomizedTopLevelProperties.Add(FName("DeltaSourceActorName"));
}

namespace PCGExActorEntryCustomization
{
	static TSharedRef<SWidget> MakePickButton(
		TSharedPtr<IPropertyHandle> ActorClassHandle,
		TSharedPtr<IPropertyHandle> DeltaSourceLevelHandle,
		TSharedPtr<IPropertyHandle> DeltaSourceActorNameHandle)
	{
		return PropertyCustomizationHelpers::MakeUseSelectedButton(
			FSimpleDelegate::CreateLambda([ActorClassHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle]()
			{
				if (!GEditor) { return; }

				USelection* Selection = GEditor->GetSelectedActors();
				if (!Selection || Selection->Num() == 0) { return; }

				AActor* SelectedActor = Cast<AActor>(Selection->GetSelectedObject(0));
				if (!SelectedActor) { return; }

				// Update actor class if it doesn't match
				if (ActorClassHandle.IsValid())
				{
					FString CurrentClassPath;
					ActorClassHandle->GetValueAsFormattedString(CurrentClassPath);

					const TSoftClassPtr<AActor> SelectedClassPath(SelectedActor->GetClass());
					if (CurrentClassPath != SelectedClassPath.ToString())
					{
						ActorClassHandle->SetValueFromFormattedString(SelectedClassPath.ToString());
					}
				}

				DeltaSourceActorNameHandle->SetValue(SelectedActor->GetFName());

				FSoftObjectPath WorldPath(SelectedActor->GetWorld());
				DeltaSourceLevelHandle->SetValueFromFormattedString(WorldPath.ToString());
			}),
			FText::FromString(TEXT("Pick the currently selected actor from the viewport")));
	}

	static TSharedRef<SWidget> MakeGoToButton(
		TSharedPtr<IPropertyHandle> DeltaSourceLevelHandle,
		TSharedPtr<IPropertyHandle> DeltaSourceActorNameHandle)
	{
		return PropertyCustomizationHelpers::MakeBrowseButton(
			FSimpleDelegate::CreateLambda([DeltaSourceLevelHandle, DeltaSourceActorNameHandle]()
			{
				if (!GEditor) { return; }

				FString LevelPathStr;
				DeltaSourceLevelHandle->GetValueAsFormattedString(LevelPathStr);

				FName ActorName;
				DeltaSourceActorNameHandle->GetValue(ActorName);

				if (LevelPathStr.IsEmpty() || ActorName == NAME_None) { return; }

				// Check if we need to load a different map
				FSoftObjectPath StoredLevelPath(LevelPathStr);
				UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
				FSoftObjectPath CurrentWorldPath(CurrentWorld);

				if (CurrentWorldPath != StoredLevelPath)
				{
					FEditorFileUtils::LoadMap(StoredLevelPath.GetLongPackageName());
				}

				// Find the actor in the (now current) world
				UWorld* World = GEditor->GetEditorWorldContext().World();
				if (!World || !World->PersistentLevel) { return; }

				AActor* FoundActor = nullptr;
				for (AActor* LevelActor : World->PersistentLevel->Actors)
				{
					if (LevelActor && LevelActor->GetFName() == ActorName)
					{
						FoundActor = LevelActor;
						break;
					}
				}

				if (FoundActor)
				{
					GEditor->SelectNone(false, true);
					GEditor->SelectActor(FoundActor, true, true);
					GEditor->MoveViewportCamerasToActor(*FoundActor, false);
				}
			}),
			FText::FromString(TEXT("Go to the delta source actor in its level")));
	}

	static bool HasDeltaSource(
		const TSharedPtr<IPropertyHandle>& DeltaSourceLevelHandle,
		const TSharedPtr<IPropertyHandle>& DeltaSourceActorNameHandle)
	{
		FString LevelStr;
		DeltaSourceLevelHandle->GetValueAsFormattedString(LevelStr);
		FName ActorName;
		DeltaSourceActorNameHandle->GetValue(ActorName);
		return FSoftObjectPath(LevelStr).IsValid() && ActorName != NAME_None;
	}
}

TSharedRef<SWidget> FPCGExActorEntryCustomization::GetAssetPicker(
	TSharedRef<IPropertyHandle> PropertyHandle,
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
{
	TSharedPtr<IPropertyHandle> SubCollection = PropertyHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> AssetHandle = PropertyHandle->GetChildHandle(GetAssetName());
	TSharedPtr<IPropertyHandle> ActorClassHandle = AssetHandle;
	TSharedPtr<IPropertyHandle> DeltaSourceLevelHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceLevel"));
	TSharedPtr<IPropertyHandle> DeltaSourceActorNameHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceActorName"));

	return SNew(SHorizontalBox)
		PCGEX_ENTRY_INDEX

		// SubCollection picker (when bIsSubCollection)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.MinWidth(200)
		.Padding(2, 0)
		[
			SNew(SBox)
			.ToolTipText(SubCollection->GetToolTipText())
			PCGEX_SUBCOLLECTION_VISIBLE
			[
				SubCollection->CreatePropertyValueWidget()
			]
		]

		// Actor class picker + pick button below (when !bIsSubCollection AND no delta source)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.MinWidth(200)
		.Padding(2, 0)
		[
			SNew(SBox)
			.Visibility_Lambda([IsSubCollectionHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle]()
			{
				bool bSub = false;
				IsSubCollectionHandle->GetValue(bSub);
				if (bSub) { return EVisibility::Collapsed; }
				return PCGExActorEntryCustomization::HasDeltaSource(DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
					? EVisibility::Collapsed : EVisibility::Visible;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					AssetHandle->CreatePropertyValueWidget()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(0, 2, 0, 0)
				[
					PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
				]
			]
		]

		// Delta source display (when !bIsSubCollection AND has delta source)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.Padding(2, 0)
		[
			SNew(SBox)
			.Visibility_Lambda([IsSubCollectionHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle]()
			{
				bool bSub = false;
				IsSubCollectionHandle->GetValue(bSub);
				if (bSub) { return EVisibility::Collapsed; }
				return PCGExActorEntryCustomization::HasDeltaSource(DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					DeltaSourceLevelHandle->CreatePropertyValueWidget()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					DeltaSourceActorNameHandle->CreatePropertyValueWidget()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 0)
				[
					PCGExActorEntryCustomization::MakeGoToButton(DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
				]
			]
		];
}

void FPCGExActorEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Render all standard children (skipping customized properties)
	FPCGExAssetEntryCustomization::CustomizeChildren(PropertyHandle, ChildBuilder, CustomizationUtils);

	TSharedPtr<IPropertyHandle> IsSubCollectionHandle = PropertyHandle->GetChildHandle(FName("bIsSubCollection"));
	TSharedPtr<IPropertyHandle> ActorClassHandle = PropertyHandle->GetChildHandle(FName("Actor"));
	TSharedPtr<IPropertyHandle> DeltaSourceLevelHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceLevel"));
	TSharedPtr<IPropertyHandle> DeltaSourceActorNameHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceActorName"));

	if (!DeltaSourceLevelHandle.IsValid() || !DeltaSourceActorNameHandle.IsValid()) { return; }

	ChildBuilder.AddCustomRow(FText::FromString("Delta Source"))
		.Visibility(MakeAttributeLambda([IsSubCollectionHandle]()
		{
			bool bIsSubCollection = false;
			if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bIsSubCollection); }
			return bIsSubCollection ? EVisibility::Collapsed : EVisibility::Visible;
		}))
		.NameContent()
		[
			DeltaSourceLevelHandle->CreatePropertyValueWidget()
		]
		.ValueContent()
		.MinDesiredWidth(300)
		[
			SNew(SHorizontalBox)

			// Actor name
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				DeltaSourceActorNameHandle->CreatePropertyValueWidget()
			]

			// Pick from selected actor
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
			]

			// Go to delta source actor
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				PCGExActorEntryCustomization::MakeGoToButton(DeltaSourceLevelHandle, DeltaSourceActorNameHandle)
			]
		];
}

#pragma endregion

#pragma region FPCGExPCGDataAssetEntryCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExPCGDataAssetEntryCustomization::MakeInstance()
{
	TSharedRef<IPropertyTypeCustomization> Ref = MakeShareable(new FPCGExPCGDataAssetEntryCustomization());
	static_cast<FPCGExPCGDataAssetEntryCustomization&>(Ref.Get()).FillCustomizedTopLevelPropertiesNames();
	return Ref;
}

void FPCGExPCGDataAssetEntryCustomization::FillCustomizedTopLevelPropertiesNames()
{
	FPCGExAssetEntryCustomization::FillCustomizedTopLevelPropertiesNames();
	CustomizedTopLevelProperties.Add(FName("Source"));
	CustomizedTopLevelProperties.Add(FName("DataAsset"));
	CustomizedTopLevelProperties.Add(FName("Level"));
}

TSharedRef<SWidget> FPCGExPCGDataAssetEntryCustomization::GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
{
	TSharedPtr<IPropertyHandle> SubCollection = PropertyHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> SourceHandle = PropertyHandle->GetChildHandle(FName("Source"));
	TSharedPtr<IPropertyHandle> DataAssetHandle = PropertyHandle->GetChildHandle(FName("DataAsset"));
	TSharedPtr<IPropertyHandle> LevelHandle = PropertyHandle->GetChildHandle(FName("Level"));

	return SNew(SHorizontalBox)
		PCGEX_ENTRY_INDEX

		// Source dropdown (hidden when subcollection)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0)
		[
			SNew(SBox)
			.ToolTipText(SourceHandle->GetToolTipText())
			PCGEX_SUBCOLLECTION_COLLAPSED
			[
				PCGExEnumCustomization::CreateRadioGroup(SourceHandle, TEXT("EPCGExDataAssetEntrySource"))
			]
		]

		// SubCollection picker
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.MinWidth(200)
		.Padding(2, 0)
		[
			SNew(SBox)
			.ToolTipText(SubCollection->GetToolTipText())
			PCGEX_SUBCOLLECTION_VISIBLE
			[
				SubCollection->CreatePropertyValueWidget()
			]
		]

		// DataAsset picker (when !subcollection && Source == DataAsset)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.MinWidth(200)
		.Padding(2, 0)
		[
			SNew(SBox)
			.ToolTipText(DataAssetHandle->GetToolTipText())
			.Visibility_Lambda([IsSubCollectionHandle, SourceHandle]()
			{
				bool bIsSubCollection = false;
				IsSubCollectionHandle->GetValue(bIsSubCollection);
				if (bIsSubCollection) { return EVisibility::Collapsed; }
				uint8 SourceValue = 0;
				SourceHandle->GetValue(SourceValue);
				return static_cast<EPCGExDataAssetEntrySource>(SourceValue) == EPCGExDataAssetEntrySource::DataAsset
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				DataAssetHandle->CreatePropertyValueWidget()
			]
		]

		// Level picker (when !subcollection && Source == Level)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.MinWidth(200)
		.Padding(2, 0)
		[
			SNew(SBox)
			.ToolTipText(LevelHandle->GetToolTipText())
			.Visibility_Lambda([IsSubCollectionHandle, SourceHandle]()
			{
				bool bIsSubCollection = false;
				IsSubCollectionHandle->GetValue(bIsSubCollection);
				if (bIsSubCollection) { return EVisibility::Collapsed; }
				uint8 SourceValue = 0;
				SourceHandle->GetValue(SourceValue);
				return static_cast<EPCGExDataAssetEntrySource>(SourceValue) == EPCGExDataAssetEntrySource::Level
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				LevelHandle->CreatePropertyValueWidget()
			]
		];
}

#pragma endregion

#undef PCGEX_SUBCOLLECTION_VISIBLE
#undef PCGEX_SUBCOLLECTION_COLLAPSED
