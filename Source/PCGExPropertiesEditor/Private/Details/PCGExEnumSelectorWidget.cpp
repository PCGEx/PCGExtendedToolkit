// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExEnumSelectorWidget.h"

#include "DetailLayoutBuilder.h"
#include "PCGExEnumSelector.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/UserDefinedEnum.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "PCGExEnumSelectorWidget"

namespace PCGExEnumSelectorWidget
{
	/**
	 * Font for items inside dropdown menus. The default detail font (9pt) is too cramped
	 * for a long-scrolling list; this is the same Regular face at a slightly larger size,
	 * which matches the readability of native UE class/asset pickers.
	 */
	static FSlateFontInfo MenuItemFont()
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", 10);
	}

	// ---------- Raw data access on the FPCGExEnumSelector property handle -------------------

	/** Returns the FPCGExEnumSelector* for each edited object. Empty array if handle invalid. */
	static TArray<FPCGExEnumSelector*> AccessSelectors(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		TArray<FPCGExEnumSelector*> Out;
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
				Out.Add(static_cast<FPCGExEnumSelector*>(Raw));
			}
		}
		return Out;
	}

	/** Returns the unanimous Class across all edited selectors, or nullptr on multi-edit conflict / no data. */
	static UEnum* ReadUnanimousClass(const TSharedRef<IPropertyHandle>& ValueHandle, bool& bOutMultipleValues)
	{
		bOutMultipleValues = false;
		const TArray<FPCGExEnumSelector*> Selectors = AccessSelectors(ValueHandle);
		if (Selectors.IsEmpty())
		{
			return nullptr;
		}

		UEnum* First = Selectors[0]->Class;
		for (int32 i = 1; i < Selectors.Num(); ++i)
		{
			if (Selectors[i]->Class != First)
			{
				bOutMultipleValues = true;
				return nullptr;
			}
		}
		return First;
	}

	/** Returns the unanimous Value across all edited selectors. Sets bOutMultipleValues if any disagree. */
	static int64 ReadUnanimousValue(const TSharedRef<IPropertyHandle>& ValueHandle, bool& bOutMultipleValues)
	{
		bOutMultipleValues = false;
		const TArray<FPCGExEnumSelector*> Selectors = AccessSelectors(ValueHandle);
		if (Selectors.IsEmpty())
		{
			return 0;
		}

		const int64 First = Selectors[0]->Value;
		for (int32 i = 1; i < Selectors.Num(); ++i)
		{
			if (Selectors[i]->Value != First)
			{
				bOutMultipleValues = true;
				return 0;
			}
		}
		return First;
	}

	/**
	 * Apply a write across all edited selectors with proper PreChange/PostChange notifications.
	 * Mutator is invoked once per edited selector with its raw pointer.
	 */
	static void ApplyWrite(
		const TSharedRef<IPropertyHandle>& ValueHandle,
		const FText& TransactionDescription,
		TFunctionRef<void(FPCGExEnumSelector&)> Mutator)
	{
		if (!ValueHandle->IsValidHandle())
		{
			return;
		}

		const TArray<FPCGExEnumSelector*> Selectors = AccessSelectors(ValueHandle);
		if (Selectors.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(TransactionDescription);
		ValueHandle->NotifyPreChange();

		for (FPCGExEnumSelector* Selector : Selectors)
		{
			Mutator(*Selector);
		}

		ValueHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		ValueHandle->NotifyFinishedChangingProperties();
	}

	// ---------- Enum metadata helpers ----------------------------------------------------------

	/**
	 * UEnums we never want to surface in the picker. These are engine internals (the macro
	 * scaffolding under /Script/CoreUObject) plus any enum explicitly tagged Hidden or
	 * BlueprintInternalUseOnly. UUserDefinedEnum (blueprint enums) are deliberately *included*.
	 */
	static bool ShouldHideEnumClass(const UEnum* Enum)
	{
		if (!Enum)
		{
			return true;
		}
		if (Enum->HasMetaData(TEXT("Hidden")))
		{
			return true;
		}
		if (Enum->HasMetaData(TEXT("BlueprintInternalUseOnly")))
		{
			return true;
		}
		return false;
	}

	// ---------- Display labels -----------------------------------------------------------------

	static FText FormatClassButtonLabel(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		bool bMulti = false;
		UEnum* Enum = ReadUnanimousClass(ValueHandle, bMulti);
		if (bMulti)
		{
			return LOCTEXT("MultipleClasses", "Multiple Values");
		}
		if (!Enum)
		{
			return LOCTEXT("NoClass", "Select Enum...");
		}
		return FText::FromString(Enum->GetName());
	}

	static FText FormatValueButtonLabel(const TSharedRef<IPropertyHandle>& ValueHandle, const bool bFlagMasks)
	{
		bool bMultiClass = false;
		UEnum* Enum = ReadUnanimousClass(ValueHandle, bMultiClass);
		if (bMultiClass || !Enum)
		{
			return LOCTEXT("NoValue", "--");
		}

		bool bMultiValue = false;
		const int64 Value = ReadUnanimousValue(ValueHandle, bMultiValue);
		if (bMultiValue)
		{
			return LOCTEXT("MultipleValues", "Multiple Values");
		}

		if (bFlagMasks && PCGExEnumSelector::IsFlagsEnum(Enum))
		{
			return PCGExEnumSelector::GetFlagsDisplayText(Enum, Value);
		}

		const int32 Index = Enum->GetIndexByValue(Value);
		if (Index == INDEX_NONE)
		{
			return FText::AsNumber(Value);
		}
		return Enum->GetDisplayNameTextByIndex(Index);
	}

	// ---------- Class picker menu --------------------------------------------------------------

	/**
	 * One row in the class picker. Native UEnums and already-loaded blueprint enums are kept
	 * as a TWeakObjectPtr; unloaded blueprint enums are kept as FAssetData and only resolved
	 * to a UEnum* on selection. This avoids force-loading the (typically hundreds of) BP
	 * enums shipped by the engine and plugins just to populate the picker.
	 */
	struct FEnumClassEntry
	{
		// Set when this entry was sourced from a loaded UEnum (native or already-loaded BP).
		TWeakObjectPtr<UEnum> Loaded;
		// Set when this entry is a lazy reference to a UUserDefinedEnum asset.
		FAssetData LazyAsset;
		// Cached display string used for both sort and search filtering. For lazy entries
		// we don't have the localized display name without loading, so the asset name is
		// used as a stand-in -- for user-authored BP enums these usually match anyway.
		FString DisplayName;

		bool IsLazy() const
		{
			return !Loaded.IsValid() && LazyAsset.IsValid();
		}

		/** Resolves to a usable UEnum*, force-loading the asset if needed. */
		UEnum* Resolve() const
		{
			if (UEnum* E = Loaded.Get())
			{
				return E;
			}
			return Cast<UEnum>(LazyAsset.GetAsset());
		}
	};

	/**
	 * Searchable menu listing all eligible UEnum classes. Picks one and writes Class to the
	 * property handle (resetting Value to 0 since the previous value is unlikely to map cleanly).
	 *
	 * Held internally by an SComboButton's menu content; lifetime ends when the menu closes.
	 */
	class SEnumClassMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEnumClassMenu)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(TWeakPtr<SComboButton>, OwningCombo)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			OwningCombo = InArgs._OwningCombo;

			BuildAllEntries();
			FilteredEntries = AllEntries;

			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(280.0f)
				.HeightOverride(400.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4)
					[
						SNew(SSearchBox)
						.OnTextChanged(this, &SEnumClassMenu::OnSearchTextChanged)
					]
					+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 4, 4)
					[
						SAssignNew(ListView, SListView<TSharedPtr<FEnumClassEntry>>)
						.ListItemsSource(&FilteredEntries)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SEnumClassMenu::OnGenerateRow)
						.OnSelectionChanged(this, &SEnumClassMenu::OnSelectionChanged)
					]
				]
			];
		}

	private:
		void BuildAllEntries()
		{
			AllEntries.Reset();

			// Pass 1 -- native UEnums and any already-loaded blueprint enums. Tracked by raw
			// pointer so we can skip the same enums when scanning the asset registry below.
			TSet<UEnum*> LoadedSeen;
			for (TObjectIterator<UEnum> It; It; ++It)
			{
				UEnum* Enum = *It;
				if (ShouldHideEnumClass(Enum))
				{
					continue;
				}
				LoadedSeen.Add(Enum);

				TSharedPtr<FEnumClassEntry> Entry = MakeShared<FEnumClassEntry>();
				Entry->Loaded = Enum;
				Entry->DisplayName = Enum->GetName();
				AllEntries.Add(Entry);
			}

			// Pass 2 -- UUserDefinedEnum assets. Listed by AssetData only; we deliberately do
			// NOT load them here. GetAsset() is deferred to OnSelectionChanged so the picker
			// is cheap to open even in projects with many BP enums.
			if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
			{
				TArray<FAssetData> UserEnumAssets;
				AssetRegistryModule->Get().GetAssetsByClass(UUserDefinedEnum::StaticClass()->GetClassPathName(), UserEnumAssets);
				for (const FAssetData& Data : UserEnumAssets)
				{
					// If the asset is already loaded it's in LoadedSeen -- skip to avoid dupes.
					// FastGetAsset(false) is the no-load fast path: returns the in-memory
					// instance via FindObject or nullptr without ever triggering a load.
					if (UEnum* Already = Cast<UEnum>(Data.FastGetAsset(false)))
					{
						if (LoadedSeen.Contains(Already))
						{
							continue;
						}
					}

					TSharedPtr<FEnumClassEntry> Entry = MakeShared<FEnumClassEntry>();
					Entry->LazyAsset = Data;
					Entry->DisplayName = Data.AssetName.ToString();
					AllEntries.Add(Entry);
				}
			}

			AllEntries.Sort([](const TSharedPtr<FEnumClassEntry>& A, const TSharedPtr<FEnumClassEntry>& B)
			{
				return A->DisplayName < B->DisplayName;
			});
		}

		void OnSearchTextChanged(const FText& NewText)
		{
			const FString Query = NewText.ToString().TrimStartAndEnd();
			if (Query.IsEmpty())
			{
				FilteredEntries = AllEntries;
			}
			else
			{
				FilteredEntries.Reset();
				for (const TSharedPtr<FEnumClassEntry>& Entry : AllEntries)
				{
					if (!Entry.IsValid())
					{
						continue;
					}
					if (Entry->DisplayName.Contains(Query, ESearchCase::IgnoreCase))
					{
						FilteredEntries.Add(Entry);
						continue;
					}
					// For loaded enums also match against the localized display name, which
					// may differ from the C++ identifier. Lazy entries skip this -- checking
					// it would force a load.
					if (const UEnum* Enum = Entry->Loaded.Get())
					{
						if (Enum->GetDisplayNameText().ToString().Contains(Query, ESearchCase::IgnoreCase))
						{
							FilteredEntries.Add(Entry);
						}
					}
				}
			}
			if (ListView.IsValid())
			{
				ListView->RequestListRefresh();
			}
		}

		TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FEnumClassEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
		{
			const UEnum* Enum = Item.IsValid() ? Item->Loaded.Get() : nullptr;
			const FString Name = Item.IsValid() ? Item->DisplayName : FString(TEXT("(missing)"));
			// Tooltip only available without loading for entries already resolved. Lazy
			// entries get an empty tooltip; the asset name itself is already the row label.
			const FText Tooltip = Enum ? Enum->GetToolTipText() : FText::GetEmpty();

			return SNew(STableRow<TSharedPtr<FEnumClassEntry>>, OwnerTable)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Name))
					.ToolTipText(Tooltip)
					.Font(MenuItemFont())
				];
		}

		void OnSelectionChanged(TSharedPtr<FEnumClassEntry> Selected, ESelectInfo::Type SelectInfo)
		{
			// Ignore programmatic selection changes from list refresh; only act on user clicks.
			if (SelectInfo == ESelectInfo::Direct)
			{
				return;
			}
			if (!Selected.IsValid())
			{
				return;
			}

			// Resolve here -- this is the one moment we accept the cost of loading a BP enum
			// asset that wasn't already in memory.
			UEnum* NewClass = Selected->Resolve();
			if (!NewClass)
			{
				return;
			}
			if (ShouldHideEnumClass(NewClass))
			{
				return;
			}
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}

			ApplyWrite(ValueHandle.ToSharedRef(), LOCTEXT("SetEnumClass", "Set Enum Class"),
			           [NewClass](FPCGExEnumSelector& Selector)
			           {
				           Selector.Class = NewClass;
				           // Previous Value is unlikely to map cleanly to a different enum -- reset.
				           Selector.Value = 0;
			           });

			if (TSharedPtr<SComboButton> Combo = OwningCombo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TWeakPtr<SComboButton> OwningCombo;
		TArray<TSharedPtr<FEnumClassEntry>> AllEntries;
		TArray<TSharedPtr<FEnumClassEntry>> FilteredEntries;
		TSharedPtr<SListView<TSharedPtr<FEnumClassEntry>>> ListView;
	};

	// ---------- Value picker menu (single-select) ----------------------------------------------

	/** Single-select enum value menu. One item per non-hidden enum index. No search box --
	 *  enum value lists are short enough to scan visually, and a search box just adds noise. */
	class SEnumValueMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEnumValueMenu)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(TWeakObjectPtr<UEnum>, EnumClass)
			SLATE_ARGUMENT(TWeakPtr<SComboButton>, OwningCombo)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			EnumClass = InArgs._EnumClass;
			OwningCombo = InArgs._OwningCombo;

			BuildEntries();

			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(220.0f)
				.MaxDesiredHeight(360.0f)
				.Padding(FMargin(4))
				[
					SNew(SListView<TSharedPtr<int32>>)
					.ListItemsSource(&Entries)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SEnumValueMenu::OnGenerateRow)
					.OnSelectionChanged(this, &SEnumValueMenu::OnSelectionChanged)
				]
			];
		}

	private:
		void BuildEntries()
		{
			Entries.Reset();
			const UEnum* Enum = EnumClass.Get();
			if (!Enum)
			{
				return;
			}
			for (int32 i = 0, Num = Enum->NumEnums(); i < Num; ++i)
			{
				if (PCGExEnumSelector::IsHiddenEntry(Enum, i))
				{
					continue;
				}
				Entries.Add(MakeShared<int32>(i));
			}
		}

		TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable)
		{
			const UEnum* Enum = EnumClass.Get();
			const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;
			const FText Display = (Enum && Index != INDEX_NONE) ? Enum->GetDisplayNameTextByIndex(Index) : FText::GetEmpty();
			const FText Tooltip = (Enum && Index != INDEX_NONE) ? Enum->GetToolTipTextByIndex(Index) : FText::GetEmpty();

			return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
				[
					SNew(STextBlock)
					.Text(Display)
					.ToolTipText(Tooltip)
					.Font(MenuItemFont())
				];
		}

		void OnSelectionChanged(TSharedPtr<int32> Selected, ESelectInfo::Type SelectInfo)
		{
			if (SelectInfo == ESelectInfo::Direct)
			{
				return;
			}
			if (!Selected.IsValid())
			{
				return;
			}

			const UEnum* Enum = EnumClass.Get();
			if (!Enum)
			{
				return;
			}
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}

			const int64 NewValue = Enum->GetValueByIndex(*Selected);

			ApplyWrite(ValueHandle.ToSharedRef(), LOCTEXT("SetEnumValue", "Set Enum Value"),
			           [NewValue](FPCGExEnumSelector& Selector)
			           {
				           Selector.Value = NewValue;
			           });

			if (TSharedPtr<SComboButton> Combo = OwningCombo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TWeakObjectPtr<UEnum> EnumClass;
		TWeakPtr<SComboButton> OwningCombo;
		TArray<TSharedPtr<int32>> Entries;
	};

	// ---------- Value picker menu (bitflags) ---------------------------------------------------

	/** Multi-select bitmask menu. Each row is a checkbox toggling one bit. */
	class SEnumBitflagsMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEnumBitflagsMenu)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(TWeakObjectPtr<UEnum>, EnumClass)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			EnumClass = InArgs._EnumClass;

			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

			const UEnum* Enum = EnumClass.Get();
			if (Enum)
			{
				for (int32 i = 0, Num = Enum->NumEnums(); i < Num; ++i)
				{
					if (PCGExEnumSelector::IsHiddenEntry(Enum, i))
					{
						continue;
					}
					const int64 Bit = PCGExEnumSelector::GetFlagMask(Enum, i);
					// Skip the "None" entry (only possible under UseEnumValuesAsMaskValuesInEditor,
					// where authors can declare a literal 0 mask). Bit-position mode can't produce
					// a zero mask -- 1<<0 = 1 -- so this check is conditionally relevant.
					if (Bit == 0)
					{
						continue;
					}

					const FText Display = Enum->GetDisplayNameTextByIndex(i);
					const FText Tooltip = Enum->GetToolTipTextByIndex(i);

					Box->AddSlot().AutoHeight().Padding(2)
					[
						SNew(SCheckBox)
						.ToolTipText(Tooltip)
						.IsChecked(this, &SEnumBitflagsMenu::IsBitChecked, Bit)
						.OnCheckStateChanged(this, &SEnumBitflagsMenu::OnBitToggled, Bit)
						[
							SNew(STextBlock)
							.Text(Display)
							.Font(MenuItemFont())
						]
					];
				}
			}

			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(220.0f)
				.MaxDesiredHeight(360.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(4)
					[
						Box
					]
				]
			];
		}

	private:
		void EnsureCache() const
		{
			if (bCacheValid)
			{
				return;
			}
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}
			CachedValue = ReadUnanimousValue(ValueHandle.ToSharedRef(), bCachedMulti);
			bCacheValid = true;
		}

		ECheckBoxState IsBitChecked(int64 Bit) const
		{
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return ECheckBoxState::Unchecked;
			}
			EnsureCache();
			if (bCachedMulti)
			{
				return ECheckBoxState::Undetermined;
			}
			return (CachedValue & Bit) == Bit ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void OnBitToggled(ECheckBoxState NewState, int64 Bit)
		{
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}
			const bool bSet = (NewState == ECheckBoxState::Checked);

			ApplyWrite(ValueHandle.ToSharedRef(), LOCTEXT("ToggleEnumBit", "Toggle Enum Flag"),
			           [Bit, bSet](FPCGExEnumSelector& Selector)
			           {
				           if (bSet)
				           {
					           Selector.Value |= Bit;
				           }
				           else
				           {
					           Selector.Value &= ~Bit;
				           }
			           });

			bCacheValid = false;
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TWeakObjectPtr<UEnum> EnumClass;

		// Cached unanimous value. IsBitChecked is called once per checkbox per Slate paint;
		// without this cache, a 32-flag enum re-walks AccessRawData 32× per paint.
		mutable int64 CachedValue = 0;
		mutable bool bCachedMulti = false;
		mutable bool bCacheValid = false;
	};

	// ---------- Top-level factory --------------------------------------------------------------

	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, bool bAllowClassPicker)
	{
		if (!ValueHandle->IsValidHandle())
		{
			return SNullWidget::NullWidget;
		}

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		// Class picker (schema-edit mode only).
		if (bAllowClassPicker)
		{
			TSharedPtr<SComboButton> ClassCombo;
			Row->AddSlot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				SAssignNew(ClassCombo, SComboButton)
				.ContentPadding(FMargin(4, 2))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([ValueHandle]()
					{
						return FormatClassButtonLabel(ValueHandle);
					})
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];

			TWeakPtr<SComboButton> WeakCombo = ClassCombo;
			ClassCombo->SetOnGetMenuContent(FOnGetContent::CreateLambda([ValueHandle, WeakCombo]() -> TSharedRef<SWidget>
			{
				return SNew(SEnumClassMenu)
					.ValueHandle(ValueHandle)
					.OwningCombo(WeakCombo);
			}));
		}

		// The bitflags menu is only for a field tagged PCGExEnumFlags holding a Bitflags enum.
		const bool bFlagMasks = ValueHandle->HasMetaData(PCGExEnumSelector::FlagsMeta);

		// Memoized by (Class, Value, bMulti): a flag label walks the whole enum on every paint.
		struct FValueLabelCache
		{
			UEnum* Class = nullptr;
			int64 Value = 0;
			bool bMulti = false;
			FText Text;
			bool bValid = false;
		};
		TSharedRef<FValueLabelCache> LabelCache = MakeShared<FValueLabelCache>();

		TSharedPtr<SComboButton> ValueCombo;
		Row->AddSlot().FillWidth(1.0f)
		[
			SAssignNew(ValueCombo, SComboButton)
			.ContentPadding(FMargin(4, 2))
			.IsEnabled_Lambda([ValueHandle]()
			{
				bool bMulti = false;
				return ReadUnanimousClass(ValueHandle, bMulti) != nullptr;
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([ValueHandle, LabelCache, bFlagMasks]()
				{
					bool bMultiClass = false;
					UEnum* Enum = ReadUnanimousClass(ValueHandle, bMultiClass);
					bool bMultiValue = false;
					const int64 Value = ReadUnanimousValue(ValueHandle, bMultiValue);
					const bool bMulti = bMultiClass || bMultiValue;

					if (LabelCache->bValid && LabelCache->Class == Enum && LabelCache->Value == Value && LabelCache->bMulti == bMulti)
					{
						return LabelCache->Text;
					}
					LabelCache->Class = Enum;
					LabelCache->Value = Value;
					LabelCache->bMulti = bMulti;
					LabelCache->Text = FormatValueButtonLabel(ValueHandle, bFlagMasks);
					LabelCache->bValid = true;
					return LabelCache->Text;
				})
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

		TWeakPtr<SComboButton> WeakValueCombo = ValueCombo;
		ValueCombo->SetOnGetMenuContent(FOnGetContent::CreateLambda([ValueHandle, WeakValueCombo, bFlagMasks]() -> TSharedRef<SWidget>
		{
			bool bMulti = false;
			UEnum* Enum = ReadUnanimousClass(ValueHandle, bMulti);
			if (!Enum)
			{
				return SNullWidget::NullWidget;
			}

			if (bFlagMasks && PCGExEnumSelector::IsFlagsEnum(Enum))
			{
				return SNew(SEnumBitflagsMenu)
					.ValueHandle(ValueHandle)
					.EnumClass(Enum);
			}

			return SNew(SEnumValueMenu)
				.ValueHandle(ValueHandle)
				.EnumClass(Enum)
				.OwningCombo(WeakValueCombo);
		}));

		return Row;
	}
}

#undef LOCTEXT_NAMESPACE
