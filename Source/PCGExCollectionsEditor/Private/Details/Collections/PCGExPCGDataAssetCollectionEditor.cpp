// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExPCGDataAssetCollectionEditor.h"

#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Engine/World.h"
#include "PCGDataAsset.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

FPCGExPCGDataAssetCollectionEditor::FPCGExPCGDataAssetCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

TSharedRef<SWidget> FPCGExPCGDataAssetCollectionEditor::BuildTilePickerWidget(
	TWeakObjectPtr<UPCGExAssetCollection> InCollection,
	int32 EntryIndex,
	FSimpleDelegate OnAssetChanged)
{
	TWeakObjectPtr<UPCGExAssetCollection> WeakColl = InCollection;
	const int32 Idx = EntryIndex;

	// Helper: read the typed entry (for Source enum access)
	auto GetTypedEntry = [WeakColl, Idx]() -> FPCGExPCGDataAssetCollectionEntry*
	{
		UPCGExAssetCollection* Coll = WeakColl.Get();
		if (!Coll) { return nullptr; }
		return static_cast<FPCGExPCGDataAssetCollectionEntry*>(Coll->EDITOR_GetMutableEntry(Idx));
	};

	// Resolve SubCollection property class from reflection
	const UClass* SubCollectionClass = nullptr;
	if (UPCGExAssetCollection* Coll = WeakColl.Get())
	{
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
		if (ArrayProp)
		{
			FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
			if (InnerProp && InnerProp->Struct)
			{
				if (const FObjectPropertyBase* SubProp = CastField<FObjectPropertyBase>(InnerProp->Struct->FindPropertyByName(FName("SubCollection"))))
				{
					SubCollectionClass = SubProp->PropertyClass;
				}
			}
		}
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// SubCollection picker (visible when bIsSubCollection is true)
	Box->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([WeakColl, Idx]()
			{
				const UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll) { return EVisibility::Collapsed; }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				return (Result.IsValid() && Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(SubCollectionClass)
				.ObjectPath_Lambda([WeakColl, Idx]() -> FString
				{
					const UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return FString(); }
					const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
					if (!Result.IsValid()) { return FString(); }
					const UPCGExAssetCollection* SubColl = Result.Entry->GetSubCollectionPtr();
					return SubColl ? SubColl->GetPathName() : FString();
				})
				.OnObjectChanged_Lambda([WeakColl, Idx, OnAssetChanged](const FAssetData& AssetData)
				{
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }
					FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
					if (!Entry) { return; }
					FScopedTransaction Transaction(INVTEXT("Set SubCollection"));
					Coll->Modify();
					Entry->InternalSubCollection = Cast<UPCGExAssetCollection>(AssetData.GetAsset());
					Coll->PostEditChange();
					OnAssetChanged.ExecuteIfBound();
				})
				.DisplayThumbnail(false)
			]
		];

	// Source enum combobox (visible when not subcollection)
	// Build a simple two-option combobox for EPCGExDataAssetEntrySource
	TSharedPtr<TArray<TSharedPtr<FString>>> SourceOptions = MakeShared<TArray<TSharedPtr<FString>>>();
	SourceOptions->Add(MakeShared<FString>(TEXT("Data Asset")));
	SourceOptions->Add(MakeShared<FString>(TEXT("Level")));

	Box->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([WeakColl, Idx]()
			{
				const UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll) { return EVisibility::Collapsed; }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				return (Result.IsValid() && !Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&(*SourceOptions))
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.OnSelectionChanged_Lambda([GetTypedEntry, WeakColl, OnAssetChanged](TSharedPtr<FString> Selected, ESelectInfo::Type SelectType)
				{
					if (!Selected.IsValid() || SelectType == ESelectInfo::Direct) { return; }
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }
					FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry) { return; }

					const EPCGExDataAssetEntrySource NewSource = (*Selected == TEXT("Level"))
						? EPCGExDataAssetEntrySource::Level
						: EPCGExDataAssetEntrySource::DataAsset;

					if (Entry->Source == NewSource) { return; }

					FScopedTransaction Transaction(INVTEXT("Change Source"));
					Coll->Modify();
					Entry->Source = NewSource;
					Coll->PostEditChange();
					OnAssetChanged.ExecuteIfBound();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([GetTypedEntry]() -> FText
					{
						const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
						if (!Entry) { return INVTEXT("?"); }
						return Entry->Source == EPCGExDataAssetEntrySource::Level
							? INVTEXT("Level") : INVTEXT("Data Asset");
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
				if (!Coll) { return EVisibility::Collapsed; }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				if (!Result.IsValid() || Result.Entry->bIsSubCollection) { return EVisibility::Collapsed; }
				const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				return (Entry && Entry->Source == EPCGExDataAssetEntrySource::DataAsset) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UPCGDataAsset::StaticClass())
				.ObjectPath_Lambda([GetTypedEntry]() -> FString
				{
					const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry) { return FString(); }
					return Entry->DataAsset.ToSoftObjectPath().ToString();
				})
				.OnObjectChanged_Lambda([GetTypedEntry, WeakColl, OnAssetChanged](const FAssetData& AssetData)
				{
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }
					FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry) { return; }
					FScopedTransaction Transaction(INVTEXT("Set DataAsset"));
					Coll->Modify();
					Entry->DataAsset = TSoftObjectPtr<UPCGDataAsset>(AssetData.GetSoftObjectPath());
					Coll->PostEditChange();
					OnAssetChanged.ExecuteIfBound();
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
				if (!Coll) { return EVisibility::Collapsed; }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				if (!Result.IsValid() || Result.Entry->bIsSubCollection) { return EVisibility::Collapsed; }
				const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
				return (Entry && Entry->Source == EPCGExDataAssetEntrySource::Level) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UWorld::StaticClass())
				.ObjectPath_Lambda([GetTypedEntry]() -> FString
				{
					const FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry) { return FString(); }
					return Entry->Level.ToSoftObjectPath().ToString();
				})
				.OnObjectChanged_Lambda([GetTypedEntry, WeakColl, OnAssetChanged](const FAssetData& AssetData)
				{
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }
					FPCGExPCGDataAssetCollectionEntry* Entry = GetTypedEntry();
					if (!Entry) { return; }
					FScopedTransaction Transaction(INVTEXT("Set Level"));
					Coll->Modify();
					Entry->Level = TSoftObjectPtr<UWorld>(AssetData.GetSoftObjectPath());
					Coll->PostEditChange();
					OnAssetChanged.ExecuteIfBound();
				})
				.DisplayThumbnail(false)
			]
		];

	return Box;
}
