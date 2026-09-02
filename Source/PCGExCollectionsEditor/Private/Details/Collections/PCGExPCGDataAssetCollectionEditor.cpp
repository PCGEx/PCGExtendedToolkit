// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExPCGDataAssetCollectionEditor.h"

#include "PCGDataAsset.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

FPCGExPCGDataAssetCollectionEditor::FPCGExPCGDataAssetCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
	SourceOptions.Add(MakeShared<FString>(TEXT("Data Asset")));
	SourceOptions.Add(MakeShared<FString>(TEXT("Level")));
	SourceOptions.Add(MakeShared<FString>(TEXT("Actor")));
}

TSharedRef<SWidget> FPCGExPCGDataAssetCollectionEditor::BuildTilePickerWidget(
	TWeakObjectPtr<UPCGExAssetCollection> InCollection,
	int32 EntryIndex,
	FOnTilePropertyEdited OnPropertyEdited)
{
	TWeakObjectPtr<UPCGExAssetCollection> WeakColl = InCollection;
	const int32 Idx = EntryIndex;

	static const FName SourcePropName = GET_MEMBER_NAME_CHECKED(FPCGExPCGDataAssetCollectionEntry, Source);
	static const FName DataAssetPropName = GET_MEMBER_NAME_CHECKED(FPCGExPCGDataAssetCollectionEntry, DataAsset);
	static const FName LevelPropName = GET_MEMBER_NAME_CHECKED(FPCGExPCGDataAssetCollectionEntry, Level);
	static const FName SourceActorPropName = GET_MEMBER_NAME_CHECKED(FPCGExPCGDataAssetCollectionEntry, SourceActor);

	auto GetTypedEntry = [WeakColl, Idx]() -> FPCGExPCGDataAssetCollectionEntry*
	{
		UPCGExAssetCollection* Coll = WeakColl.Get();
		if (!Coll)
		{
			return nullptr;
		}
		return static_cast<FPCGExPCGDataAssetCollectionEntry*>(Coll->EDITOR_GetMutableEntry(Idx));
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	Box->AddSlot()
	   .AutoHeight()
	[
		BuildSubCollectionPickerSlot(WeakColl, Idx, OnPropertyEdited)
	];

	// Source enum combobox (visible when not subcollection)
	// SourceOptions lives on this editor instance -- SComboBox stores a raw pointer into it.
	Box->AddSlot()
	   .AutoHeight()
	   .Padding(0, 0, 0, 2)
	[
		SNew(SBox)
		.Visibility_Lambda([WeakColl, Idx]()
		{
			const UPCGExAssetCollection* Coll = WeakColl.Get();
			if (!Coll)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
			return (Result.IsValid() && !Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&SourceOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
			{
				return SNew(STextBlock)
					.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
			})
			.OnSelectionChanged_Lambda([GetTypedEntry, WeakColl, OnPropertyEdited](TSharedPtr<FString> Selected, ESelectInfo::Type SelectType)
			{
				if (!Selected.IsValid() || SelectType == ESelectInfo::Direct)
				{
					return;
				}
				UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll)
				{
					return;
				}
				FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return;
				}

				EPCGExDataAssetEntrySource NewSource = EPCGExDataAssetEntrySource::DataAsset;
				if (*Selected == TEXT("Level"))
				{
					NewSource = EPCGExDataAssetEntrySource::Level;
				}
				else if (*Selected == TEXT("Actor"))
				{
					NewSource = EPCGExDataAssetEntrySource::Actor;
				}

				if (Entry->Source == NewSource)
				{
					return;
				}

				FScopedTransaction Transaction(INVTEXT("Change Source"));
				Coll->Modify();
				Entry->Source = NewSource;
				Coll->PostEditChange();
				OnPropertyEdited.ExecuteIfBound(SourcePropName);
			})
			[
				SNew(STextBlock)
				.Text_Lambda([GetTypedEntry]() -> FText
				{
					const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry)
					{
						return INVTEXT("?");
					}
					switch (Entry->Source)
					{
					case EPCGExDataAssetEntrySource::Level:
						return INVTEXT("Level");
					case EPCGExDataAssetEntrySource::Actor:
						return INVTEXT("Actor");
					default:
						return INVTEXT("Data Asset");
					}
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		]
	];

	// DataAsset picker (when Source == DataAsset and not subcollection)
	Box->AddSlot()
	   .AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([GetTypedEntry, WeakColl, Idx]()
		{
			const UPCGExAssetCollection* Coll = WeakColl.Get();
			if (!Coll)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
			if (!Result.IsValid() || Result.Entry->bIsSubCollection)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
			return (Entry && Entry->Source == EPCGExDataAssetEntrySource::DataAsset) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UPCGDataAsset::StaticClass())
			.ObjectPath_Lambda([GetTypedEntry]() -> FString
			{
				const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return FString();
				}
				return Entry->DataAsset.ToSoftObjectPath().ToString();
			})
			.OnObjectChanged_Lambda([GetTypedEntry, WeakColl, OnPropertyEdited](const FAssetData& AssetData)
			{
				UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll)
				{
					return;
				}
				FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return;
				}
				FScopedTransaction Transaction(INVTEXT("Set DataAsset"));
				Coll->Modify();
				Entry->DataAsset = TSoftObjectPtr<UPCGDataAsset>(AssetData.GetSoftObjectPath());
				Coll->PostEditChange();
				OnPropertyEdited.ExecuteIfBound(DataAssetPropName);
			})
			.DisplayThumbnail(false)
		]
	];

	// Level picker (when Source == Level and not subcollection)
	Box->AddSlot()
	   .AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([GetTypedEntry, WeakColl, Idx]()
		{
			const UPCGExAssetCollection* Coll = WeakColl.Get();
			if (!Coll)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
			if (!Result.IsValid() || Result.Entry->bIsSubCollection)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
			return (Entry && Entry->Source == EPCGExDataAssetEntrySource::Level) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UWorld::StaticClass())
			.ObjectPath_Lambda([GetTypedEntry]() -> FString
			{
				const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return FString();
				}
				return Entry->Level.ToSoftObjectPath().ToString();
			})
			.OnObjectChanged_Lambda([GetTypedEntry, WeakColl, OnPropertyEdited](const FAssetData& AssetData)
			{
				UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll)
				{
					return;
				}
				FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return;
				}
				FScopedTransaction Transaction(INVTEXT("Set Level"));
				Coll->Modify();
				Entry->Level = TSoftObjectPtr<UWorld>(AssetData.GetSoftObjectPath());
				Coll->PostEditChange();
				OnPropertyEdited.ExecuteIfBound(LevelPropName);
			})
			.DisplayThumbnail(false)
		]
	];

	// Actor picker (when Source == Actor and not subcollection). SObjectPropertyEntryBox on an
	// actor class offers the level actor picker.
	Box->AddSlot()
	   .AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([GetTypedEntry, WeakColl, Idx]()
		{
			const UPCGExAssetCollection* Coll = WeakColl.Get();
			if (!Coll)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
			if (!Result.IsValid() || Result.Entry->bIsSubCollection)
			{
				return EVisibility::Collapsed;
			}
			const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
			return (Entry && Entry->Source == EPCGExDataAssetEntrySource::Actor) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(AActor::StaticClass())
			.ObjectPath_Lambda([GetTypedEntry]() -> FString
			{
				const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return FString();
				}
				return Entry->SourceActor.ToSoftObjectPath().ToString();
			})
			.OnObjectChanged_Lambda([GetTypedEntry, WeakColl, OnPropertyEdited](const FAssetData& AssetData)
			{
				UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll)
				{
					return;
				}
				FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				if (!Entry)
				{
					return;
				}
				FScopedTransaction Transaction(INVTEXT("Set Source Actor"));
				Coll->Modify();
				Entry->SourceActor = TSoftObjectPtr<AActor>(AssetData.GetSoftObjectPath());
				Coll->PostEditChange();
				OnPropertyEdited.ExecuteIfBound(SourceActorPropName);
			})
			.DisplayThumbnail(false)
		]
	];

	return Box;
}
