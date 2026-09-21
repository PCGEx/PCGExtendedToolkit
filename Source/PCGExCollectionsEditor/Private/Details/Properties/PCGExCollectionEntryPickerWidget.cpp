// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/PCGExCollectionEntryPickerWidget.h"

#include "AssetThumbnail.h"
#include "DetailLayoutBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorSlateUtils.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Properties/PCGExProperty_CollectionEntry.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "PCGExCollectionEntryPickerWidget"

namespace PCGExCollectionEntryPickerWidget
{
	// ---------- Options registry ------------------------------------------------------------------

	using FProviderKey = TPair<FName, FName>;

	static TMap<FProviderKey, FOptionsProvider>& GetProviders()
	{
		static TMap<FProviderKey, FOptionsProvider> Providers;
		return Providers;
	}

	void RegisterOptionsProvider(const FName OwnerStructName, const FName PropertyName, FOptionsProvider Provider)
	{
		GetProviders().Add(FProviderKey(OwnerStructName, PropertyName), MoveTemp(Provider));
	}

	void UnregisterOptionsProvider(const FName OwnerStructName, const FName PropertyName)
	{
		GetProviders().Remove(FProviderKey(OwnerStructName, PropertyName));
	}

	FOptions ResolveOptions(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		FOptions Options;
		const FProperty* Property = ValueHandle->IsValidHandle() ? ValueHandle->GetProperty() : nullptr;
		const UStruct* Owner = Property ? Property->GetOwnerStruct() : nullptr;
		if (!Owner)
		{
			return Options;
		}
		if (const FOptionsProvider* Provider = GetProviders().Find(FProviderKey(Owner->GetFName(), Property->GetFName())))
		{
			(*Provider)(ValueHandle, Options);
		}
		return Options;
	}

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
	static UPCGExAssetCollection* LoadCollectionForEditing(const TSoftObjectPtr<UPCGExAssetCollection>& SoftCollection)
	{
		if (SoftCollection.IsNull())
		{
			return nullptr;
		}
		PCGExHelpers::LoadBlocking_AnyThreadTpl(SoftCollection);
		UPCGExAssetCollection* Collection = SoftCollection.Get();
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

	// ---------- Type compatibility ---------------------------------------------------------------

	static bool IsEntryCompatible(const FPCGExAssetCollectionEntry* Entry, TConstArrayView<PCGExAssetCollection::FTypeId> AllowedTypes)
	{
		if (!Entry)
		{
			return false;
		}
		if (AllowedTypes.IsEmpty())
		{
			return true;
		}
		for (const PCGExAssetCollection::FTypeId& TypeId : AllowedTypes)
		{
			if (Entry->IsType(TypeId))
			{
				return true;
			}
		}
		return false;
	}

	/** ShortName label of a registered type id ("Mesh"), the id itself when unregistered. */
	static FText TypeLabel(const PCGExAssetCollection::FTypeId TypeId)
	{
		PCGExAssetCollection::FTypeInfo Info;
		if (PCGExAssetCollection::FTypeRegistry::Get().GetInfo(TypeId, Info) && Info.EntryStruct)
		{
			return PCGExCollectionEditorUtils::GetEntryTypeLabel(Info.EntryStruct);
		}
		return FText::FromName(TypeId);
	}

	static FText FormatAllowedTypes(TConstArrayView<PCGExAssetCollection::FTypeId> AllowedTypes)
	{
		TArray<FString> Labels;
		for (const PCGExAssetCollection::FTypeId& TypeId : AllowedTypes)
		{
			Labels.Add(TypeLabel(TypeId).ToString());
		}
		return FText::FromString(FString::Join(Labels, TEXT(", ")));
	}

	/** Why an entry cannot be picked under AllowedTypes. Only meaningful when IsEntryCompatible is false. */
	static FText IncompatibleReason(const FPCGExAssetCollectionEntry* Entry, TConstArrayView<PCGExAssetCollection::FTypeId> AllowedTypes)
	{
		if (Entry && Entry->bIsSubCollection)
		{
			return LOCTEXT("IncompatibleSubCollection", "Subcollection rows cannot be picked.");
		}
		return FText::Format(LOCTEXT("IncompatibleType", "This is a {0} entry; this pick requires: {1}."),
		                     Entry ? TypeLabel(Entry->GetTypeId()) : FText::GetEmpty(), FormatAllowedTypes(AllowedTypes));
	}

	/** Collection classes that can hold an entry of an allowed type: typed hosts of those lineages plus heterogeneous hosts. */
	static TArray<const UClass*> ComputeHostClasses(TConstArrayView<PCGExAssetCollection::FTypeId> AllowedTypes)
	{
		using namespace PCGExAssetCollection;

		struct FRow
		{
			FTypeId Id;
			const UClass* Class;
			const UScriptStruct* EntryStruct;
		};

		// Copied out under the registry lock: IsA re-enters it and the lock is not recursive.
		TArray<FRow> Rows;
		FTypeRegistry::Get().ForEach([&Rows](const FTypeInfo& Info)
		{
			if (Info.CollectionClass.IsValid())
			{
				Rows.Add({Info.Id, Info.CollectionClass.Get(), Info.EntryStruct});
			}
		});

		TArray<const UClass*> Out;
		for (const FRow& Row : Rows)
		{
			const bool bHeterogeneous = !Row.EntryStruct || Row.EntryStruct == FPCGExAssetCollectionEntry::StaticStruct();
			bool bAccepts = bHeterogeneous;
			for (int32 i = 0; !bAccepts && i < AllowedTypes.Num(); ++i)
			{
				bAccepts = FTypeRegistry::Get().IsA(Row.Id, AllowedTypes[i]);
			}
			if (bAccepts)
			{
				Out.Add(Row.Class);
			}
		}
		return Out;
	}

	static bool IsDeclaredSource(const FOptions& Options, const TSoftObjectPtr<UPCGExAssetCollection>& Collection)
	{
		if (Options.Sources.IsEmpty())
		{
			return true;
		}
		for (const TSoftObjectPtr<UPCGExAssetCollection>& Source : Options.Sources)
		{
			if (Source == Collection)
			{
				return true;
			}
		}
		return false;
	}

	// ---------- Current pick ---------------------------------------------------------------------

	enum class EPickState : uint8
	{
		Multiple,
		NoCollection,
		Unset,
		Unloaded,
		Orphaned,
		Incompatible,
		UndeclaredSource,
		Ok,
	};

	struct FPickInfo
	{
		EPickState State = EPickState::Unset;
		FPCGExCollectionEntryRef Ref;
		/** Resident collection; null unless the state is past Unloaded. */
		const UPCGExAssetCollection* Collection = nullptr;
		int32 RawIndex = INDEX_NONE;

		bool IsFlagged() const
		{
			return State == EPickState::Orphaned || State == EPickState::Incompatible || State == EPickState::UndeclaredSource;
		}
	};

	/** Resident-only resolve (no load on paint). */
	static FPickInfo EvaluatePick(const TSharedRef<IPropertyHandle>& ValueHandle, const FOptions& Options)
	{
		FPickInfo Info;
		bool bMultiple = false;
		Info.Ref = ReadUnanimous(ValueHandle, bMultiple);
		if (bMultiple)
		{
			Info.State = EPickState::Multiple;
			return Info;
		}
		if (Info.Ref.Collection.IsNull())
		{
			Info.State = EPickState::NoCollection;
			return Info;
		}
		if (Info.Ref.EntryId == 0)
		{
			Info.State = EPickState::Unset;
			return Info;
		}
		Info.Collection = Info.Ref.Collection.Get();
		if (!Info.Collection)
		{
			Info.State = EPickState::Unloaded;
			return Info;
		}
		Info.RawIndex = Info.Collection->FindRawIndexByEntryId(Info.Ref.EntryId);
		if (Info.RawIndex == INDEX_NONE)
		{
			Info.State = EPickState::Orphaned;
			return Info;
		}
		if (!IsEntryCompatible(Info.Collection->GetEntryRaw(Info.RawIndex).Entry, Options.AllowedEntryTypes))
		{
			Info.State = EPickState::Incompatible;
			return Info;
		}
		if (!IsDeclaredSource(Options, Info.Ref.Collection))
		{
			Info.State = EPickState::UndeclaredSource;
			return Info;
		}
		Info.State = EPickState::Ok;
		return Info;
	}

	static FText FormatEntryButtonLabel(const FPickInfo& Info, const FOptions& Options)
	{
		switch (Info.State)
		{
		case EPickState::Multiple:
			return LOCTEXT("MultipleValues", "Multiple Values");
		case EPickState::NoCollection:
			return Options.Sources.IsEmpty() ? LOCTEXT("NoCollection", "No collection") : LOCTEXT("NoEntry", "None");
		case EPickState::Unset:
			return LOCTEXT("NoEntry", "None");
		default:
			break;
		}

		// Ids are minted from GetTypeHash(FGuid) and half of them are negative -- unsigned hex
		// reads as the opaque token it is, not as a signed count.
		const FText IdText = FText::FromString(FString::Printf(TEXT("%08X"), static_cast<uint32>(Info.Ref.EntryId)));
		switch (Info.State)
		{
		case EPickState::Unloaded:
			return FText::Format(LOCTEXT("EntryUnloaded", "Entry {0} (not loaded)"), IdText);
		case EPickState::Orphaned:
			return FText::Format(LOCTEXT("EntryOrphaned", "Orphaned ({0})"), IdText);
		case EPickState::Incompatible:
			return FText::Format(LOCTEXT("EntryIncompatible", "{0}  [{1}]  (wrong type)"), EntryLabel(Info.Collection, Info.RawIndex), Info.RawIndex);
		case EPickState::UndeclaredSource:
			return FText::Format(LOCTEXT("EntryUndeclaredSource", "{0}  [{1}]  (undeclared source)"), EntryLabel(Info.Collection, Info.RawIndex), Info.RawIndex);
		default:
			return FText::Format(LOCTEXT("EntryLabelWithIndex", "{0}  [{1}]"), EntryLabel(Info.Collection, Info.RawIndex), Info.RawIndex);
		}
	}

	static FText FormatPickTooltip(const FPickInfo& Info, const FOptions& Options)
	{
		const FText CollectionName = FText::FromString(Info.Ref.Collection.GetAssetName());
		switch (Info.State)
		{
		case EPickState::Orphaned:
			return FText::Format(LOCTEXT("TooltipOrphaned", "No entry with this id exists in {0} anymore."), CollectionName);
		case EPickState::Incompatible:
			return IncompatibleReason(Info.Collection->GetEntryRaw(Info.RawIndex).Entry, Options.AllowedEntryTypes);
		case EPickState::UndeclaredSource:
			return FText::Format(LOCTEXT("TooltipUndeclaredSource", "{0} is not among the declared source collections."), CollectionName);
		default:
			if (!Info.Ref.Collection.IsNull())
			{
				return FText::FromString(Info.Ref.Collection.ToString());
			}
			return Options.AllowedEntryTypes.IsEmpty()
				       ? FText::GetEmpty()
				       : FText::Format(LOCTEXT("TooltipAccepts", "Accepts: {0}"), FormatAllowedTypes(Options.AllowedEntryTypes));
		}
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

		FName Category = NAME_None;
		/** Effective grammar symbol (may come from the collection's global grammar); None = no badge. */
		FName Symbol = NAME_None;
		FLinearColor SymbolColor = FLinearColor::White;

		/** Host the entry lives in; written with the id when the menu spans declared sources. */
		FSoftObjectPath CollectionPath;
		int32 SourceIndex = 0;

		/** False when the entry's type is outside the allowed set: dimmed, never selectable. */
		bool bCompatible = true;
		FText IncompatibleReason;

		/** Header rows: label only, never selectable. */
		bool bIsHeader = false;
		bool bIsSourceHeader = false;
	};

	/** Dropdown listing every entry of the picked collection (or of every declared source). Lives while the combo menu is open. */
	class SEntryMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEntryMenu)
				: _bWriteCollection(false)
			{
			}

			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ValueHandle)
			SLATE_ARGUMENT(TArray<TWeakObjectPtr<UPCGExAssetCollection>>, Collections)
			SLATE_ARGUMENT(TArray<PCGExAssetCollection::FTypeId>, AllowedTypes)
			/** Sources mode: a pick also writes the entry's collection. */
			SLATE_ARGUMENT(bool, bWriteCollection)
			SLATE_ARGUMENT(TWeakPtr<SComboButton>, OwningCombo)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueHandle = InArgs._ValueHandle;
			Collections = InArgs._Collections;
			AllowedTypes = InArgs._AllowedTypes;
			bWriteCollection = InArgs._bWriteCollection;
			OwningCombo = InArgs._OwningCombo;
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

			BuildItems();
			OnFilterChanged(FText::GetEmpty());

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
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 4, 2, 0)
					[
						// Per-user preference (editor settings), so it holds across pickers and sessions.
						SNew(SCheckBox)
						.Visibility(bAnyIncompatible ? EVisibility::Visible : EVisibility::Collapsed)
						.IsChecked_Lambda([]() { return GetDefault<UPCGExCollectionsEditorSettings>()->bHideIncompatiblePickerEntries ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged(this, &SEntryMenu::OnHideIncompatibleChanged)
						.ToolTipText(FText::Format(LOCTEXT("HideIncompatibleTooltip", "Hide entries this pick cannot use (accepts: {0})."), FormatAllowedTypes(AllowedTypes)))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HideIncompatible", "Hide incompatible entries"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
				]
			];
		}

	private:
		void BuildItems()
		{
			Items.Reset();
			bHasCategories = false;
			bAnyIncompatible = false;

			for (int32 SourceIndex = 0; SourceIndex < Collections.Num(); ++SourceIndex)
			{
				const UPCGExAssetCollection* Coll = Collections[SourceIndex].Get();
				if (!Coll)
				{
					continue;
				}

				TArray<TSharedPtr<FEntryItem>> SourceItems;
				const FSoftObjectPath CollectionPath(Coll);
				Coll->ForEachEntry([this, Coll, SourceIndex, &CollectionPath, &SourceItems](const FPCGExAssetCollectionEntry* Entry, const int32 RawIndex)
				{
					TSharedPtr<FEntryItem> Item = MakeShared<FEntryItem>();
					Item->RawIndex = RawIndex;
					Item->EntryId = Entry->EntryId;
					Item->ThumbPath = Entry->EDITOR_GetThumbnailAssetPath();
					const FString AssetName = Item->ThumbPath.GetAssetName();
					Item->Label = AssetName.IsEmpty() ? FText::Format(LOCTEXT("EntryByIndex", "Entry {0}"), RawIndex) : FText::FromString(AssetName);
					Item->Category = Entry->Category;
					Item->CollectionPath = CollectionPath;
					Item->SourceIndex = SourceIndex;
					Item->bCompatible = IsEntryCompatible(Entry, AllowedTypes);
					if (!Item->bCompatible)
					{
						Item->IncompatibleReason = IncompatibleReason(Entry, AllowedTypes);
						bAnyIncompatible = true;
					}
					bHasCategories |= !Entry->Category.IsNone();

					// Effective, not raw: a subcollection routing to the host's global grammar shows what
					// actually prints -- the same resolve the grid tiles badge with.
					if (const FPCGExAssetGrammarDetails* Grammar = Entry->GetEffectiveGrammar(Coll))
					{
						Item->Symbol = Grammar->Symbol;
						Item->SymbolColor = Grammar->DebugColor;
					}
					SourceItems.Add(Item);
				});

				SortByCategory(SourceItems);
				Items.Append(MoveTemp(SourceItems));
			}
		}

		/** Categories sort by name, uncategorized last; within a group, collection order (the displayed raw index) holds. */
		static void SortByCategory(TArray<TSharedPtr<FEntryItem>>& InOutItems)
		{
			bool bAny = false;
			for (const TSharedPtr<FEntryItem>& Item : InOutItems)
			{
				bAny |= !Item->Category.IsNone();
			}
			// A flat collection stays a flat list.
			if (!bAny)
			{
				return;
			}

			TArray<FName> Order;
			for (const TSharedPtr<FEntryItem>& Item : InOutItems)
			{
				Order.AddUnique(Item->Category);
			}
			Order.Sort([](const FName& A, const FName& B)
			{
				if (A.IsNone() != B.IsNone())
				{
					return B.IsNone(); // uncategorized sinks to the bottom
				}
				return A.LexicalLess(B);
			});
			TArray<TSharedPtr<FEntryItem>> Sorted;
			Sorted.Reserve(InOutItems.Num());
			for (const FName& Category : Order)
			{
				for (const TSharedPtr<FEntryItem>& Item : InOutItems)
				{
					if (Item->Category == Category)
					{
						Sorted.Add(Item);
					}
				}
			}
			InOutItems = MoveTemp(Sorted);
		}

		static FText CategoryLabel(const FName InCategory)
		{
			return InCategory.IsNone() ? LOCTEXT("Uncategorized", "Uncategorized") : FText::FromName(InCategory);
		}

		FText SourceLabel(const int32 SourceIndex) const
		{
			const UPCGExAssetCollection* Coll = Collections.IsValidIndex(SourceIndex) ? Collections[SourceIndex].Get() : nullptr;
			return Coll ? FText::FromString(Coll->GetName()) : LOCTEXT("UnknownSource", "<unknown>");
		}

		bool ShouldHideIncompatible() const
		{
			return !AllowedTypes.IsEmpty() && GetDefault<UPCGExCollectionsEditorSettings>()->bHideIncompatiblePickerEntries;
		}

		void OnHideIncompatibleChanged(const ECheckBoxState NewState)
		{
			GetMutableDefault<UPCGExCollectionsEditorSettings>()->SetHideIncompatiblePickerEntries(NewState == ECheckBoxState::Checked);
			OnFilterChanged(CurrentFilter);
		}

		void OnFilterChanged(const FText& InText)
		{
			CurrentFilter = InText;
			const FString Needle = InText.ToString();
			const bool bHideIncompatible = ShouldHideIncompatible();
			const bool bMultiSource = Collections.Num() > 1;

			Filtered.Reset();
			int32 OpenSource = INDEX_NONE;
			FName OpenCategory;
			bool bAnyGroupOpen = false;
			for (const TSharedPtr<FEntryItem>& Item : Items)
			{
				if (bHideIncompatible && !Item->bCompatible)
				{
					continue;
				}
				// Symbol and category match too: "wall" finds the group and the grammar alike.
				const bool bMatches = Needle.IsEmpty()
					|| Item->Label.ToString().Contains(Needle)
					|| (!Item->Symbol.IsNone() && Item->Symbol.ToString().Contains(Needle))
					|| (!Item->Category.IsNone() && Item->Category.ToString().Contains(Needle));
				if (!bMatches)
				{
					continue;
				}
				// Headers are synthesized per surviving group, so an emptied-out source or category never lingers.
				if (bMultiSource && OpenSource != Item->SourceIndex)
				{
					OpenSource = Item->SourceIndex;
					bAnyGroupOpen = false;
					TSharedPtr<FEntryItem> Header = MakeShared<FEntryItem>();
					Header->bIsHeader = true;
					Header->bIsSourceHeader = true;
					Header->SourceIndex = Item->SourceIndex;
					Header->Label = SourceLabel(Item->SourceIndex);
					Filtered.Add(Header);
				}
				if (bHasCategories && (!bAnyGroupOpen || OpenCategory != Item->Category))
				{
					bAnyGroupOpen = true;
					OpenCategory = Item->Category;
					TSharedPtr<FEntryItem> Header = MakeShared<FEntryItem>();
					Header->bIsHeader = true;
					Header->SourceIndex = Item->SourceIndex;
					Header->Category = Item->Category;
					Header->Label = CategoryLabel(Item->Category);
					Filtered.Add(Header);
				}
				Filtered.Add(Item);
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
			const bool bMultiSource = Collections.Num() > 1;
			const FTableRowStyle* NoHoverStyle = &FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.NoHoverTableRow");

			if (Item->bIsHeader)
			{
				// No hover, no selection affordance: a label, not an option.
				return SNew(STableRow<TSharedPtr<FEntryItem>>, OwnerTable)
					.Style(NoHoverStyle)
					.Padding(FMargin(Item->bIsSourceHeader ? 2 : (bMultiSource ? 10 : 2), 4, 2, 1))
					[
						SNew(STextBlock)
						.Text(Item->Label)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", Item->bIsSourceHeader ? 9 : 8))
						.ColorAndOpacity(Item->bIsSourceHeader ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
					];
			}

			TSharedRef<SVerticalBox> NameBox = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Item->Label)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				];

			// The grammar badge, styled exactly as the grid tiles' (shared brush + readable-text rule),
			// so an entry reads the same in the picker as in the collection editor.
			if (!Item->Symbol.IsNone())
			{
				FLinearColor Tint = Item->SymbolColor;
				Tint.A = 0.85f;
				NameBox->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 1, 0, 0)
				[
					SNew(SBorder)
					.BorderImage(PCGExCollectionEditorSlateUtils::GetBadgeBrush())
					.BorderBackgroundColor(FSlateColor(Tint))
					.Padding(FMargin(3, 1))
					[
						SNew(STextBlock)
						.Text(FText::FromName(Item->Symbol))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(PCGExCollectionEditorSlateUtils::PickReadableTextColor(Item->SymbolColor))
					]
				];
			}

			// Incompatible rows stay hoverable (the reason tooltip needs it) but read as inert: dimmed, no hover style.
			const float Indent = 2.0f + (bHasCategories ? 8.0f : 0.0f) + (bMultiSource ? 8.0f : 0.0f);
			return SNew(STableRow<TSharedPtr<FEntryItem>>, OwnerTable)
				.Style(Item->bCompatible ? &FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row") : NoHoverStyle)
				.Padding(FMargin(Indent, 2, 2, 2))
				.ToolTipText(Item->bCompatible ? FText::GetEmpty() : Item->IncompatibleReason)
				[
					SNew(SBorder)
					.BorderImage(FStyleDefaults::GetNoBrush())
					.ColorAndOpacity(Item->bCompatible ? FLinearColor::White : FLinearColor(1, 1, 1, 0.35f))
					.Padding(0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
						[
							MakeThumbnail(Item->ThumbPath)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							NameBox
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(Item->RawIndex))
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				];
		}

		void OnSelectionChanged(TSharedPtr<FEntryItem> Selected, ESelectInfo::Type SelectInfo)
		{
			if (SelectInfo == ESelectInfo::Direct || !Selected.IsValid())
			{
				return;
			}
			if (Selected->bIsHeader || !Selected->bCompatible)
			{
				// A label or an inert row, not an option: bounce the selection off.
				if (ListView.IsValid())
				{
					ListView->ClearSelection();
				}
				return;
			}
			if (!ValueHandle.IsValid() || !ValueHandle->IsValidHandle())
			{
				return;
			}

			const int32 NewId = Selected->EntryId;
			const TSoftObjectPtr<UPCGExAssetCollection> NewCollection(Selected->CollectionPath);
			const bool bWriteColl = bWriteCollection;
			ApplyWrite(ValueHandle.ToSharedRef(), LOCTEXT("SetCollectionEntry", "Set Collection Entry"),
			           [NewId, &NewCollection, bWriteColl](FPCGExCollectionEntryRef& Ref)
			           {
				           if (bWriteColl)
				           {
					           Ref.Collection = NewCollection;
				           }
				           Ref.EntryId = NewId;
			           });

			if (const TSharedPtr<SComboButton> Combo = OwningCombo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		}

		TSharedPtr<IPropertyHandle> ValueHandle;
		TArray<TWeakObjectPtr<UPCGExAssetCollection>> Collections;
		TArray<PCGExAssetCollection::FTypeId> AllowedTypes;
		bool bWriteCollection = false;
		TWeakPtr<SComboButton> OwningCombo;
		TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
		TSharedPtr<SListView<TSharedPtr<FEntryItem>>> ListView;
		TArray<TSharedPtr<FEntryItem>> Items;
		TArray<TSharedPtr<FEntryItem>> Filtered;
		FText CurrentFilter;
		bool bHasCategories = false;
		bool bAnyIncompatible = false;
	};

	// ---------- Row ------------------------------------------------------------------------------

	static TSharedRef<SWidget> MakeCollectionBox(const TSharedRef<IPropertyHandle>& ValueHandle, const TSharedRef<FOptions>& Options)
	{
		// Snapshot: the Content Browser runs the filter per asset while scrolling. Empty = no type constraint.
		const bool bFilterHosts = !Options->AllowedEntryTypes.IsEmpty();
		const TArray<const UClass*> HostClasses = bFilterHosts ? ComputeHostClasses(Options->AllowedEntryTypes) : TArray<const UClass*>();

		return SNew(SObjectPropertyEntryBox)
			.AllowedClass(UPCGExAssetCollection::StaticClass())
			.AllowClear(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(false)
			.DisplayUseSelected(true)
			.OnShouldFilterAsset_Lambda([HostClasses, bFilterHosts](const FAssetData& AssetData)
			{
				if (!bFilterHosts)
				{
					return false;
				}
				const UClass* AssetClass = AssetData.GetClass();
				if (!AssetClass)
				{
					return true;
				}
				for (const UClass* HostClass : HostClasses)
				{
					if (AssetClass->IsChildOf(HostClass))
					{
						return false;
					}
				}
				return true;
			})
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

	static TSharedRef<SWidget> MakeLockToggle(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		return SNew(SCheckBox)
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
			];
	}

	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, const FOptions& InOptions)
	{
		if (!ValueHandle->IsValidHandle())
		{
			return SNullWidget::NullWidget;
		}

		// Shared by every lambda below; the options are fixed for the widget's lifetime.
		const TSharedRef<FOptions> Options = MakeShared<FOptions>(InOptions);
		const bool bSourcesMode = !Options->Sources.IsEmpty();
		const bool bSchemaEdit = Options->bSchemaEdit;

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		if (!bSourcesMode)
		{
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
					MakeCollectionBox(ValueHandle, Options)
				]
			];

			if (bSchemaEdit && Options->bShowLock)
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					MakeLockToggle(ValueHandle)
				];
			}
		}

		TSharedPtr<SComboButton> EntryCombo;
		Row->AddSlot().FillWidth(1.0f)
		[
			SAssignNew(EntryCombo, SComboButton)
			.ContentPadding(FMargin(4, 2))
			.IsEnabled_Lambda([ValueHandle, bSourcesMode]()
			{
				bool bMultiple = false;
				const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
				return !bMultiple && (bSourcesMode || !Ref.Collection.IsNull());
			})
			.ToolTipText_Lambda([ValueHandle, Options]() { return FormatPickTooltip(EvaluatePick(ValueHandle, *Options), *Options); })
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
					.Text_Lambda([ValueHandle, Options]() { return FormatEntryButtonLabel(EvaluatePick(ValueHandle, *Options), *Options); })
					.ColorAndOpacity_Lambda([ValueHandle, Options]()
					{
						return EvaluatePick(ValueHandle, *Options).IsFlagged() ? FSlateColor(FLinearColor(1.0f, 0.35f, 0.35f)) : FSlateColor::UseForeground();
					})
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		];

		TWeakPtr<SComboButton> WeakCombo = EntryCombo;
		EntryCombo->SetOnGetMenuContent(FOnGetContent::CreateLambda([ValueHandle, WeakCombo, Options, bSourcesMode]() -> TSharedRef<SWidget>
		{
			TArray<TWeakObjectPtr<UPCGExAssetCollection>> Collections;
			if (bSourcesMode)
			{
				for (const TSoftObjectPtr<UPCGExAssetCollection>& Source : Options->Sources)
				{
					if (UPCGExAssetCollection* Collection = LoadCollectionForEditing(Source))
					{
						Collections.Add(Collection);
					}
				}
			}
			else
			{
				bool bMultiple = false;
				const FPCGExCollectionEntryRef Ref = ReadUnanimous(ValueHandle, bMultiple);
				if (UPCGExAssetCollection* Collection = bMultiple ? nullptr : LoadCollectionForEditing(Ref.Collection))
				{
					Collections.Add(Collection);
				}
			}
			if (Collections.IsEmpty())
			{
				return SNew(SBox).Padding(8)
				[
					SNew(STextBlock)
					.Text(bSourcesMode ? LOCTEXT("NoSourceLoaded", "No declared source collection could be loaded.") : LOCTEXT("NoCollectionLoaded", "The collection could not be loaded."))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
			}
			return SNew(SEntryMenu)
				.ValueHandle(ValueHandle)
				.Collections(Collections)
				.AllowedTypes(Options->AllowedEntryTypes)
				.bWriteCollection(bSourcesMode)
				.OwningCombo(WeakCombo);
		}));

		return Row;
	}

	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, const bool bSchemaEdit)
	{
		FOptions Options;
		Options.bSchemaEdit = bSchemaEdit;
		return Make(ValueHandle, Options);
	}
}

#undef LOCTEXT_NAMESPACE
