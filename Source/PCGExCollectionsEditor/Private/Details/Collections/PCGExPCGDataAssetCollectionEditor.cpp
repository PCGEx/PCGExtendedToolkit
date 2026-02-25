// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExPCGDataAssetCollectionEditor.h"

#include "PropertyHandle.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

FPCGExPCGDataAssetCollectionEditor::FPCGExPCGDataAssetCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

TSharedRef<SWidget> FPCGExPCGDataAssetCollectionEditor::BuildTilePickerWidget(TSharedRef<IPropertyHandle> EntryHandle)
{
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle = EntryHandle->GetChildHandle(FName("bIsSubCollection"));
	TSharedPtr<IPropertyHandle> SubCollectionHandle = EntryHandle->GetChildHandle(FName("SubCollection"));
	TSharedPtr<IPropertyHandle> SourceHandle = EntryHandle->GetChildHandle(FName("Source"));
	TSharedPtr<IPropertyHandle> DataAssetHandle = EntryHandle->GetChildHandle(FName("DataAsset"));
	TSharedPtr<IPropertyHandle> LevelHandle = EntryHandle->GetChildHandle(FName("Level"));

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// SubCollection picker
	if (SubCollectionHandle.IsValid())
	{
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([IsSubCollectionHandle]()
				{
					bool bSub = false;
					if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bSub); }
					return bSub ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SubCollectionHandle->CreatePropertyValueWidget()
				]
			];
	}

	// Source enum (visible when not subcollection)
	if (SourceHandle.IsValid())
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 2)
			[
				SNew(SBox)
				.Visibility_Lambda([IsSubCollectionHandle]()
				{
					bool bSub = false;
					if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bSub); }
					return bSub ? EVisibility::Collapsed : EVisibility::Visible;
				})
				[
					SourceHandle->CreatePropertyValueWidget()
				]
			];
	}

	// DataAsset picker (when Source == DataAsset)
	if (DataAssetHandle.IsValid())
	{
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([IsSubCollectionHandle, SourceHandle]()
				{
					bool bSub = false;
					if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bSub); }
					if (bSub) { return EVisibility::Collapsed; }
					uint8 SourceVal = 0;
					if (SourceHandle.IsValid()) { SourceHandle->GetValue(SourceVal); }
					return static_cast<EPCGExDataAssetEntrySource>(SourceVal) == EPCGExDataAssetEntrySource::DataAsset
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					DataAssetHandle->CreatePropertyValueWidget()
				]
			];
	}

	// Level picker (when Source == Level)
	if (LevelHandle.IsValid())
	{
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([IsSubCollectionHandle, SourceHandle]()
				{
					bool bSub = false;
					if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bSub); }
					if (bSub) { return EVisibility::Collapsed; }
					uint8 SourceVal = 0;
					if (SourceHandle.IsValid()) { SourceHandle->GetValue(SourceVal); }
					return static_cast<EPCGExDataAssetEntrySource>(SourceVal) == EPCGExDataAssetEntrySource::Level
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					LevelHandle->CreatePropertyValueWidget()
				]
			];
	}

	return Box;
}
