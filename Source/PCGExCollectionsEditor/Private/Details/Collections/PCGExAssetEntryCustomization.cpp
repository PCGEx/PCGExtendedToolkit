// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExAssetEntryCustomization.h"

#include "UObject/Package.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Details/Collections/PCGExGenericAssetPickerCustomization.h"
#include "Selection.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Core/PCGExAssetCollection.h"
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

	const UPCGExCollectionsEditorSettings* EditorSettings = GetDefault<UPCGExCollectionsEditorSettings>();

	for (uint32 i = 0; i < NumElements; ++i)
	{
		TSharedPtr<IPropertyHandle> ElementHandle = PropertyHandle->GetChildHandle(i);
		FName ElementName = ElementHandle ? ElementHandle->GetProperty()->GetFName() : NAME_None;
		if (!ElementHandle.IsValid() || CustomizedTopLevelProperties.Contains(ElementName))
		{
			continue;
		}

		// Build-time filter instead of a per-row dynamic Visibility attribute: that attribute
		// breaks change-notification/write-back for default-expanded nested structs in the grid's
		// FStructOnScope panel (Scale to Fit / Justification never reached OnFinishedChangingProperties).
		// ForceRefreshTabs re-runs this customization whenever the filter toggles
		// (OnHiddenAssetPropertyNamesChanged), so show/hide stays live without it.
		if (EditorSettings->GetPropertyVisibility(ElementName) != EVisibility::Visible)
		{
			continue;
		}

		ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
	}

	// Add PropertyOverrides WITHOUT any visibility filter or customization
	// The visibility lambda interferes with nested customizations - prevents value widgets from rendering
	// PCGExPropertiesEditor module handles all PropertyOverrides UI via registered customizations
	TSharedPtr<IPropertyHandle> PropertyOverridesHandle = PropertyHandle->GetChildHandle(TEXT("PropertyOverrides"));
	if (PropertyOverridesHandle.IsValid())
	{
		ChildBuilder.AddProperty(PropertyOverridesHandle.ToSharedRef());
	}

	// Same reason as above: the grammar struct customizations host an async color picker
	// (FColorPicker modal) whose OnColorCommitted write-back fails when the row is wrapped
	// in a dynamic-visibility lambda. EditCondition meta on these properties still hides
	// them appropriately based on bIsSubCollection / SubGrammarMode.
	TSharedPtr<IPropertyHandle> AssetGrammarHandle = PropertyHandle->GetChildHandle(TEXT("AssetGrammar"));
	if (AssetGrammarHandle.IsValid())
	{
		ChildBuilder.AddProperty(AssetGrammarHandle.ToSharedRef());
	}
	TSharedPtr<IPropertyHandle> CollectionGrammarHandle = PropertyHandle->GetChildHandle(TEXT("CollectionGrammar"));
	if (CollectionGrammarHandle.IsValid())
	{
		ChildBuilder.AddProperty(CollectionGrammarHandle.ToSharedRef());
	}
}

void FPCGExAssetEntryCustomization::FillCustomizedTopLevelPropertiesNames()
{
	CustomizedTopLevelProperties.Add(FName("Weight"));
	CustomizedTopLevelProperties.Add(FName("Category"));
	CustomizedTopLevelProperties.Add(FName("bIsSubCollection"));
	CustomizedTopLevelProperties.Add(FName("SubCollection"));
	CustomizedTopLevelProperties.Add(FName("PropertyOverrides")); // Handled separately - no visibility filter
	CustomizedTopLevelProperties.Add(FName("AssetGrammar"));      // Handled separately - no visibility filter
	CustomizedTopLevelProperties.Add(FName("CollectionGrammar")); // Handled separately - no visibility filter
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
	CustomizedTopLevelProperties.Add(FName("DeltaSourceActor"));
}

namespace PCGExActorEntryCustomization
{
	static TSharedRef<SWidget> MakePickButton(
		TSharedPtr<IPropertyHandle> ActorClassHandle,
		TSharedPtr<IPropertyHandle> DeltaSourceActorHandle)
	{
		return PropertyCustomizationHelpers::MakeUseSelectedButton(
			FSimpleDelegate::CreateLambda([ActorClassHandle, DeltaSourceActorHandle]()
			{
				if (!GEditor)
				{
					return;
				}

				USelection* Selection = GEditor->GetSelectedActors();
				if (!Selection || Selection->Num() == 0)
				{
					return;
				}

				AActor* SelectedActor = Cast<AActor>(Selection->GetSelectedObject(0));
				if (!SelectedActor)
				{
					return;
				}

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

				DeltaSourceActorHandle->SetValueFromFormattedString(FSoftObjectPath(SelectedActor).ToString());
			}),
			FText::FromString(TEXT("Pick the currently selected actor from the viewport")));
	}

	static FSoftObjectPath GetDeltaSourcePath(const TSharedPtr<IPropertyHandle>& DeltaSourceActorHandle)
	{
		FString PathStr;
		DeltaSourceActorHandle->GetValueAsFormattedString(PathStr);
		return FSoftObjectPath(PathStr);
	}

	static TSharedRef<SWidget> MakeGoToButton(TSharedPtr<IPropertyHandle> DeltaSourceActorHandle)
	{
		return PropertyCustomizationHelpers::MakeBrowseButton(
			FSimpleDelegate::CreateLambda([DeltaSourceActorHandle]()
			{
				if (!GEditor)
				{
					return;
				}

				const FSoftObjectPath ActorPath = GetDeltaSourcePath(DeltaSourceActorHandle);
				if (!ActorPath.IsValid())
				{
					return;
				}

				// Open the actor's map when another one is current; the path then resolves live.
				UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
				if (!CurrentWorld || CurrentWorld->GetPackage()->GetFName() != ActorPath.GetLongPackageFName())
				{
					FEditorFileUtils::LoadMap(ActorPath.GetLongPackageName());
				}

				if (AActor* FoundActor = Cast<AActor>(ActorPath.ResolveObject()))
				{
					GEditor->SelectNone(false, true);
					GEditor->SelectActor(FoundActor, true, true);
					GEditor->MoveViewportCamerasToActor(*FoundActor, false);
				}
			}),
			FText::FromString(TEXT("Go to the delta source actor in its level")));
	}

	static bool HasDeltaSource(const TSharedPtr<IPropertyHandle>& DeltaSourceActorHandle)
	{
		return GetDeltaSourcePath(DeltaSourceActorHandle).IsValid();
	}
}

TSharedRef<SWidget> FPCGExActorEntryCustomization::GetAssetPicker(
	TSharedRef<IPropertyHandle> PropertyHandle,
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
{
	TSharedPtr<IPropertyHandle> SubCollection = PropertyHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> AssetHandle = PropertyHandle->GetChildHandle(GetAssetName());
	TSharedPtr<IPropertyHandle> ActorClassHandle = AssetHandle;
	TSharedPtr<IPropertyHandle> DeltaSourceActorHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceActor"));

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
				.Visibility_Lambda([IsSubCollectionHandle, DeltaSourceActorHandle]()
				{
					bool bSub = false;
					IsSubCollectionHandle->GetValue(bSub);
					if (bSub)
					{
						return EVisibility::Collapsed;
					}
					return PCGExActorEntryCustomization::HasDeltaSource(DeltaSourceActorHandle)
						? EVisibility::Collapsed
						: EVisibility::Visible;
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
						PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceActorHandle)
					]
				]
			]

			// Delta source display (when !bIsSubCollection AND has delta source)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.Padding(2, 0)
			[
				SNew(SBox)
				.Visibility_Lambda([IsSubCollectionHandle, DeltaSourceActorHandle]()
				{
					bool bSub = false;
					IsSubCollectionHandle->GetValue(bSub);
					if (bSub)
					{
						return EVisibility::Collapsed;
					}
					return PCGExActorEntryCustomization::HasDeltaSource(DeltaSourceActorHandle)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1)
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						DeltaSourceActorHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceActorHandle)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						PCGExActorEntryCustomization::MakeGoToButton(DeltaSourceActorHandle)
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
	TSharedPtr<IPropertyHandle> DeltaSourceActorHandle = PropertyHandle->GetChildHandle(FName("DeltaSourceActor"));

	if (!DeltaSourceActorHandle.IsValid())
	{
		return;
	}

	ChildBuilder.AddCustomRow(FText::FromString("Delta Source"))
	            .Visibility(MakeAttributeLambda([IsSubCollectionHandle]()
	            {
		            bool bIsSubCollection = false;
		            if (IsSubCollectionHandle.IsValid())
		            {
			            IsSubCollectionHandle->GetValue(bIsSubCollection);
		            }
		            return bIsSubCollection ? EVisibility::Collapsed : EVisibility::Visible;
	            }))
	            .NameContent()
		[
			DeltaSourceActorHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(300)
		[
			SNew(SHorizontalBox)

			// Actor reference
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				DeltaSourceActorHandle->CreatePropertyValueWidget()
			]

			// Pick from selected actor
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				PCGExActorEntryCustomization::MakePickButton(ActorClassHandle, DeltaSourceActorHandle)
			]

			// Go to delta source actor
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				PCGExActorEntryCustomization::MakeGoToButton(DeltaSourceActorHandle)
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
	CustomizedTopLevelProperties.Add(FName("SourceActor"));
}

TSharedRef<SWidget> FPCGExPCGDataAssetEntryCustomization::GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
{
	TSharedPtr<IPropertyHandle> SubCollection = PropertyHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> SourceHandle = PropertyHandle->GetChildHandle(FName("Source"));
	TSharedPtr<IPropertyHandle> DataAssetHandle = PropertyHandle->GetChildHandle(FName("DataAsset"));
	TSharedPtr<IPropertyHandle> LevelHandle = PropertyHandle->GetChildHandle(FName("Level"));
	TSharedPtr<IPropertyHandle> SourceActorHandle = PropertyHandle->GetChildHandle(FName("SourceActor"));

	auto VisibleForSource = [IsSubCollectionHandle, SourceHandle](const EPCGExDataAssetEntrySource Wanted)
	{
		return [IsSubCollectionHandle, SourceHandle, Wanted]()
		{
			bool bIsSubCollection = false;
			IsSubCollectionHandle->GetValue(bIsSubCollection);
			if (bIsSubCollection)
			{
				return EVisibility::Collapsed;
			}
			uint8 SourceValue = 0;
			SourceHandle->GetValue(SourceValue);
			return static_cast<EPCGExDataAssetEntrySource>(SourceValue) == Wanted
				? EVisibility::Visible
				: EVisibility::Collapsed;
		};
	};

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
				.Visibility_Lambda(VisibleForSource(EPCGExDataAssetEntrySource::DataAsset))
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
				.Visibility_Lambda(VisibleForSource(EPCGExDataAssetEntrySource::Level))
				[
					LevelHandle->CreatePropertyValueWidget()
				]
			]

			// Actor picker (when !subcollection && Source == Actor)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.MinWidth(200)
			.Padding(2, 0)
			[
				SNew(SBox)
				.ToolTipText(SourceActorHandle->GetToolTipText())
				.Visibility_Lambda(VisibleForSource(EPCGExDataAssetEntrySource::Actor))
				[
					SourceActorHandle->CreatePropertyValueWidget()
				]
			];
}

#pragma endregion

#pragma region FPCGExGenericEntryCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExGenericEntryCustomization::MakeInstance()
{
	TSharedRef<IPropertyTypeCustomization> Ref = MakeShareable(new FPCGExGenericEntryCustomization());
	static_cast<FPCGExGenericEntryCustomization&>(Ref.Get()).FillCustomizedTopLevelPropertiesNames();
	return Ref;
}

// Asset-hosted views only (Entries tab); struct-on-scope panels customize the Asset property instead.
TSharedRef<SWidget> FPCGExGenericEntryCustomization::GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle)
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
					PCGExGenericAssetPicker::MakeFilteredAssetPicker(AssetHandle.ToSharedRef())
				]
			];
}

#pragma endregion

#undef PCGEX_SUBCOLLECTION_VISIBLE
#undef PCGEX_SUBCOLLECTION_COLLAPSED
