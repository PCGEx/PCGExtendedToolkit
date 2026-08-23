// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/PCGExCollectionEntryPickerWidget.h"

#include "AssetThumbnail.h"
#include "DetailLayoutBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Properties/PCGExProperty_CollectionEntry.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "PCGExCollectionEntryPickerWidget"

namespace PCGExCollectionEntryPickerWidget
{
	// ---------- Raw data access on the FPCGExCollectionEntryRef property handle ------------------

	static TArray<FPCGExCollectionEntryRef*> AccessRefs(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		TArray<FPCGExCollectionEntryRef*> Out;
		if (!ValueHandle->IsValidHandle())
		{
			return Out;
		}

		TArray<void*> RawData;
		ValueHandle->AccessRawData(RawData);
		Out.Reserve(RawData.Num());
		for (void* Raw : RawData)
		{
			if (Raw)
			{
				Out.Add(static_cast<FPCGExCollectionEntryRef*>(Raw));
			}
		}
		return Out;
	}

	/** Unanimous value across edited objects; bOutMultiple set (and a default returned) on disagreement. */
	static FPCGExCollectionEntryRef ReadUnanimous(const TSharedRef<IPropertyHandle>& ValueHandle, bool& bOutMultiple)
	{
		bOutMultiple = false;
		const TArray<FPCGExCollectionEntryRef*> Refs = AccessRefs(ValueHandle);
		if (Refs.IsEmpty())
		{
			return FPCGExCollectionEntryRef();
		}
		for (int32 i = 1; i < Refs.Num(); ++i)
		{
			if (!(*Refs[i] == *Refs[0]))
			{
				bOutMultiple = true;
				return FPCGExCollectionEntryRef();
			}
		}
		return *Refs[0];
	}

	static void ApplyWrite(
		const TSharedRef<IPropertyHandle>& ValueHandle,
		const FText& TransactionDescription,
		TFunctionRef<void(FPCGExCollectionEntryRef&)> Mutator)
	{
		const TArray<FPCGExCollectionEntryRef*> Refs = AccessRefs(ValueHandle);
		if (Refs.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(TransactionDescription);
		ValueHandle->NotifyPreChange();
		for (FPCGExCollectionEntryRef* Ref : Refs)
		{
			Mutator(*Ref);
		}
		ValueHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		ValueHandle->NotifyFinishedChangingProperties();
	}

	/** Editor-side resolve: loads the collection if needed (game thread) and mints missing EntryIds. */
	static UPCGExAssetCollection* LoadCollectionForEditing(const FPCGExCollectionEntryRef& Ref)
	{
		if (Ref.Collection.IsNull())
		{
			return nullptr;
		}
		PCGExHelpers::LoadBlocking_AnyThreadTpl(Ref.Collection);
		UPCGExAssetCollection* Collection = Ref.Collection.Get();
		PCGExCollectionEditorUtils::EnsureEntryIds(Collection, /*bNotify=*/true);
		return Collection;
	}

	static FText EntryLabel(const UPCGExAssetCollection* Collection, const int32 RawIndex)
	{
		const FPCGExAssetCollectionEntry* Entry = Collection ? Collection->GetEntryRaw(RawIndex).Entry : nullptr;
		if (!Entry)
		{
			return LOCTEXT("MissingEntry", "<missing>");
		}
		const FString AssetName = Entry->EDITOR_GetThumbnailAssetPath().GetAssetName();
		return AssetName.IsEmpty() ? FText::Format(LOCTEXT("EntryByIndex", "Entry {0}"), RawIndex) : FText::FromString(AssetName);
	}

	/** Button label for the current pick; resolves only against an already-loaded collection (no load on paint). */
	static FText FormatEntryButtonLabel(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		bool bMultiple = false;
		const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
		if (bMultiple)
		{
			return LOCTEXT("MultipleValues", "Multiple Values");
		}
		if (Ref.Collection.IsNull())
		{
			return LOCTEXT("NoCollection", "No collection");
		}
		if (Ref.EntryId == 0)
		{
			return LOCTEXT("NoEntry", "None");
		}
		const UPCGExAssetCollection* Collection = Ref.Collection.Get();
		if (!Collection)
		{
			return FText::Format(LOCTEXT("EntryUnloaded", "Entry {0} (not loaded)"), Ref.EntryId);
		}
		const int32 RawIndex = Collection->FindRawIndexByEntryId(Ref.EntryId);
		if (RawIndex == INDEX_NONE)
		{
			return FText::Format(LOCTEXT("EntryOrphaned", "Orphaned ({0})"), Ref.EntryId);
		}
		return FText::Format(LOCTEXT("EntryLabelWithIndex", "{0}  [{1}]"), EntryLabel(Collection, RawIndex), RawIndex);
	}

	static bool IsPickOrphaned(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		bool bMultiple = false;
		const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
		const UPCGExAssetCollection* Collection = (!bMultiple && Ref.EntryId != 0) ? Ref.Collection.Get() : nullptr;
		return Collection && Collection->FindRawIndexByEntryId(Ref.EntryId) == INDEX_NONE;
	}

	/** Thumbnail asset path of the current pick; empty when unresolved. Cheap (resident-only resolve). */
	static FSoftObjectPath ResolvePickThumbPath(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		bool bMultiple = false;
		const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
		const UPCGExAssetCollection* Collection = (!bMultiple && Ref.EntryId != 0) ? Ref.Collection.Get() : nullptr;
		if (!Collection)
		{
			return FSoftObjectPath();
		}
		const int32 RawIndex = Collection->FindRawIndexByEntryId(Ref.EntryId);
		const FPCGExAssetCollectionEntry* Entry = RawIndex != INDEX_NONE ? Collection->GetEntryRaw(RawIndex).Entry : nullptr;
		return Entry ? Entry->EDITOR_GetThumbnailAssetPath() : FSoftObjectPath();
	}

	/** Small live thumbnail of the current pick: polls the resolved thumb path and swaps content on change. */
	class SPickThumbnail : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPickThumbnail)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(float, Size)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			Size = InArgs._Size;
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(2);
			SetCanTick(true);

			ChildSlot
			[
				SAssignNew(Host, SBox)
				.WidthOverride(Size)
				.HeightOverride(Size)
			];
			Refresh();
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			Refresh();
		}

	private:
		void Refresh()
		{
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}
			const FSoftObjectPath Path = ResolvePickThumbPath(ValueHandle.ToSharedRef());
			if (Path == CurrentPath && bBuilt)
			{
				return;
			}
			CurrentPath = Path;
			bBuilt = true;

			if (Path.IsNull())
			{
				Host->SetContent(SNullWidget::NullWidget);
				return;
			}
			const TSharedRef<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				PCGExCollectionEditorUtils::ResolveEntryAssetData(Path), FMath::RoundToInt32(Size), FMath::RoundToInt32(Size), ThumbnailPool);
			FAssetThumbnailConfig Config;
			Config.bAllowFadeIn = false;
			Host->SetContent(Thumbnail->MakeThumbnailWidget(Config));
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
		TSharedPtr<SBox> Host;
		FSoftObjectPath CurrentPath;
		float Size = 20.0f;
		bool bBuilt = false;
	};

	// ---------- Entry menu -----------------------------------------------------------------------

	struct FEntryItem
	{
		int32 RawIndex = INDEX_NONE;
		int32 EntryId = 0;
		FSoftObjectPath ThumbPath;
		FText Label;
	};

	/** Dropdown listing every entry of the picked collection. Lives while the combo menu is open. */
	class SEntryMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEntryMenu)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(TWeakObjectPtr<UPCGExAssetCollection>, Collection)
			SLATE_ARGUMENT(TWeakPtr<SComboButton>, OwningCombo)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			Collection = InArgs._Collection;
			OwningCombo = InArgs._OwningCombo;
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

			BuildItems();
			Filtered = Items;

			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(320.0f)
				.MaxDesiredHeight(420.0f)
				.Padding(FMargin(4))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
					[
						SNew(SSearchBox)
						.OnTextChanged(this, &SEntryMenu::OnFilterChanged)
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(ListView, SListView<TSharedPtr<FEntryItem>>)
						.ListItemsSource(&Filtered)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SEntryMenu::OnGenerateRow)
						.OnSelectionChanged(this, &SEntryMenu::OnSelectionChanged)
					]
				]
			];
		}

	private:
		void BuildItems()
		{
			Items.Reset();
			const UPCGExAssetCollection* Coll = Collection.Get();
			if (!Coll)
			{
				return;
			}
			Coll->ForEachEntry([this](const FPCGExAssetCollectionEntry* Entry, const int32 RawIndex)
			{
				TSharedPtr<FEntryItem> Item = MakeShared<FEntryItem>();
				Item->RawIndex = RawIndex;
				Item->EntryId = Entry->EntryId;
				Item->ThumbPath = Entry->EDITOR_GetThumbnailAssetPath();
				const FString AssetName = Item->ThumbPath.GetAssetName();
				Item->Label = AssetName.IsEmpty() ? FText::Format(LOCTEXT("EntryByIndex", "Entry {0}"), RawIndex) : FText::FromString(AssetName);
				Items.Add(Item);
			});
		}

		void OnFilterChanged(const FText& InText)
		{
			const FString Needle = InText.ToString();
			Filtered.Reset();
			for (const TSharedPtr<FEntryItem>& Item : Items)
			{
				if (Needle.IsEmpty() || Item->Label.ToString().Contains(Needle))
				{
					Filtered.Add(Item);
				}
			}
			if (ListView.IsValid())
			{
				ListView->RequestListRefresh();
			}
		}

		TSharedRef<SWidget> MakeThumbnail(const FSoftObjectPath& Path) const
		{
			constexpr int32 Size = 32;
			if (Path.IsNull())
			{
				return SNew(SBox).WidthOverride(Size).HeightOverride(Size);
			}
			const TSharedRef<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				PCGExCollectionEditorUtils::ResolveEntryAssetData(Path), Size, Size, ThumbnailPool);
			FAssetThumbnailConfig Config;
			Config.bAllowFadeIn = false;
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[Thumbnail->MakeThumbnailWidget(Config)];
		}

		TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FEntryItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
		{
			return SNew(STableRow<TSharedPtr<FEntryItem>>, OwnerTable)
				.Padding(FMargin(2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						MakeThumbnail(Item->ThumbPath)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Item->Label)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(Item->RawIndex))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				];
		}

		void OnSelectionChanged(TSharedPtr<FEntryItem> Selected, ESelectInfo::Type SelectInfo)
		{
			if (SelectInfo == ESelectInfo::Direct || !Selected.IsValid())
			{
				return;
			}
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}

			const int32 NewId = Selected->EntryId;
			ApplyWrite(ValueHandle.ToSharedRef(), LOCTEXT("SetCollectionEntry", "Set Collection Entry"),
			           [NewId](FPCGExCollectionEntryRef& Ref)
			           {
				           Ref.EntryId = NewId;
			           });

			if (const TSharedPtr<SComboButton> Combo = OwningCombo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TWeakObjectPtr<UPCGExAssetCollection> Collection;
		TWeakPtr<SComboButton> OwningCombo;
		TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
		TSharedPtr<SListView<TSharedPtr<FEntryItem>>> ListView;
		TArray<TSharedPtr<FEntryItem>> Items;
		TArray<TSharedPtr<FEntryItem>> Filtered;
	};

	// ---------- Row ------------------------------------------------------------------------------

	static TSharedRef<SWidget> MakeCollectionBox(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		return SNew(SObjectPropertyEntryBox)
			.AllowedClass(UPCGExAssetCollection::StaticClass())
			.AllowClear(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(false)
			.DisplayUseSelected(true)
			.ObjectPath_Lambda([ValueHandle]()
			{
				bool bMultiple = false;
				const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
				return bMultiple ? FString() : Ref.Collection.ToSoftObjectPath().ToString();
			})
			.OnObjectChanged_Lambda([ValueHandle](const FAssetData& AssetData)
			{
				const TSoftObjectPtr<UPCGExAssetCollection> NewCollection(AssetData.GetSoftObjectPath());
				ApplyWrite(ValueHandle, LOCTEXT("SetCollection", "Set Collection"),
				           [&NewCollection](FPCGExCollectionEntryRef& Ref)
				           {
					           if (Ref.Collection != NewCollection)
					           {
						           Ref.Collection = NewCollection;
						           Ref.EntryId = 0;
					           }
				           });
			});
	}

	static bool ReadUnanimousLock(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		// Mixed lock states read as locked -- the safe (non-retargeting) presentation.
		bool bMultiple = false;
		const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
		return bMultiple || Ref.bLockCollection;
	}

	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, const bool bSchemaEdit)
	{
		if (!ValueHandle->IsValidHandle())
		{
			return SNullWidget::NullWidget;
		}

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		// Collection box: schema authoring always shows it; override rows only while unlocked.
		// Visibility-bound (not build-time) so a schema lock toggle takes effect without a rebuild.
		Row->AddSlot().FillWidth(1.0f).Padding(0, 0, 4, 0)
		[
			SNew(SBox)
			.Visibility_Lambda([ValueHandle, bSchemaEdit]()
			{
				return (bSchemaEdit || !ReadUnanimousLock(ValueHandle)) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MakeCollectionBox(ValueHandle)
			]
		];

		if (bSchemaEdit)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.Padding(FMargin(4.0f, 2.0f))
				.ToolTipText(LOCTEXT("LockCollectionTooltip", "Lock the collection: overrides can only pick an entry within it. Unlocked, overrides may retarget to another collection."))
				.IsChecked_Lambda([ValueHandle]()
				{
					return ReadUnanimousLock(ValueHandle) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([ValueHandle](const ECheckBoxState NewState)
				{
					const bool bLocked = NewState == ECheckBoxState::Checked;
					ApplyWrite(ValueHandle, LOCTEXT("SetCollectionLock", "Set Collection Lock"),
					           [bLocked](FPCGExCollectionEntryRef& Ref)
					           {
						           Ref.bLockCollection = bLocked;
					           });
				})
				[
					SNew(SImage)
					.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
					.Image_Lambda([ValueHandle]()
					{
						return FAppStyle::GetBrush(ReadUnanimousLock(ValueHandle) ? TEXT("Icons.Lock") : TEXT("Icons.Unlock"));
					})
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		TSharedPtr<SComboButton> EntryCombo;
		Row->AddSlot().FillWidth(1.0f)
		[
			SAssignNew(EntryCombo, SComboButton)
			.ContentPadding(FMargin(4, 2))
			.IsEnabled_Lambda([ValueHandle]()
			{
				bool bMultiple = false;
				const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
				return !bMultiple && !Ref.Collection.IsNull();
			})
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(SPickThumbnail)
					.ValueHandle(TSharedPtr<IPropertyHandle>(ValueHandle))
					.Size(20.0f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([ValueHandle]() { return FormatEntryButtonLabel(ValueHandle); })
					.ColorAndOpacity_Lambda([ValueHandle]()
					{
						return IsPickOrphaned(ValueHandle) ? FSlateColor(FLinearColor(1.0f, 0.35f, 0.35f)) : FSlateColor::UseForeground();
					})
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		];

		TWeakPtr<SComboButton> WeakCombo = EntryCombo;
		EntryCombo->SetOnGetMenuContent(FOnGetContent::CreateLambda([ValueHandle, WeakCombo]() -> TSharedRef<SWidget>
		{
			bool bMultiple = false;
			const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
			UPCGExAssetCollection* Collection = bMultiple ? nullptr : LoadCollectionForEditing(Ref);
			if (!Collection)
			{
				return SNullWidget::NullWidget;
			}
			return SNew(SEntryMenu)
				.ValueHandle(ValueHandle)
				.Collection(Collection)
				.OwningCombo(WeakCombo);
		}));

		return Row;
	}
}

#undef LOCTEXT_NAMESPACE
