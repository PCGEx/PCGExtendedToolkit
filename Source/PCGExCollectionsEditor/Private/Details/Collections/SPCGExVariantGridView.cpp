// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExVariantGridView.h"

#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IStructureDetailsView.h"
#include "Misc/ITransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Details/Collections/PCGExCollectionEditorSlateUtils.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/SPCGExCollectionCategoryGroup.h"
#include "Helpers/PCGExStreamingHelpers.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExVariantGridView"

namespace PCGExVariantGrid
{
	// Group display name for a source slot: asset name, or a placeholder for unset slots.
	inline FName MakeGroupName(const TSoftObjectPtr<UPCGExAssetCollection>& Source, const int32 GroupIdx)
	{
		if (Source.IsNull())
		{
			return FName(*FString::Printf(TEXT("Unset Source [%d]"), GroupIdx));
		}
		return FName(*Source.ToSoftObjectPath().GetAssetName());
	}

	// Synthetic group hosting the asset-path swap rules.
	inline const FName AssetSwapsGroupName = FName(TEXT("Asset Swaps"));

	// Any collection subtype except the variant itself (other variants ARE legal --
	// daisy-chained swap nodes). Registry-only class check, no asset load.
	inline bool CanBeSource(const FAssetData& InAsset, const UPCGExVariantCollection* InVariant)
	{
		return InVariant
			&& InAsset.IsInstanceOf(UPCGExAssetCollection::StaticClass())
			&& InAsset.GetSoftObjectPath() != FSoftObjectPath(InVariant);
	}
}

#pragma region SPCGExVariantGridTile

void SPCGExVariantGridTile::Construct(const FArguments& InArgs)
{
	TileSize = InArgs._TileSize;
	ItemIndex = InArgs._ItemIndex;
	Item = InArgs._Item;
	ThumbnailPool = InArgs._ThumbnailPool;
	ThumbnailCachePtr = InArgs._ThumbnailCachePtr;
	OnTileClicked = InArgs._OnTileClicked;
	OnDeclareSwap = InArgs._OnDeclareSwap;
	OnRevokeSwap = InArgs._OnRevokeSwap;

	// The root (and its selection highlight) persists across content rebuilds — only the
	// inner overlay re-instantiates when display data changes.
	ChildSlot
	[
		SAssignNew(RootBorder, SBorder)
		.Padding(2.f)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() -> FSlateColor
		{
			return bIsSelected ? FSlateColor(FLinearColor(0.9f, 0.6f, 0.1f, 1.f)) : FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));
		})
	];

	RebuildContent();
}

void SPCGExVariantGridTile::UpdateItem(const FPCGExVariantGridItem& InItem)
{
	const bool bDisplayChanged =
		Item.GetState() != InItem.GetState()
		|| Item.SourceThumbPath != InItem.SourceThumbPath
		|| Item.OverrideThumbPath != InItem.OverrideThumbPath
		|| !Item.Label.EqualTo(InItem.Label)
		|| Item.SourceSymbol != InItem.SourceSymbol
		|| Item.OverrideSymbol != InItem.OverrideSymbol
		|| Item.SourceSymbolColor != InItem.SourceSymbolColor
		|| Item.OverrideSymbolColor != InItem.OverrideSymbolColor;

	Item = InItem;

	if (bDisplayChanged)
	{
		RebuildContent();
	}
}

void SPCGExVariantGridTile::RebuildContent()
{
	const EPCGExVariantTileState State = Item.GetState();
	const float BadgeSize = FMath::RoundToFloat(TileSize * 0.33f);

	// Main thumbnail: replacement whenever one applies, source otherwise.
	const bool bShowsReplacement =
		State == EPCGExVariantTileState::Swapped ||
		State == EPCGExVariantTileState::SwappedByRule ||
		State == EPCGExVariantTileState::RuleDefinition;
	const FSoftObjectPath& MainPath = bShowsReplacement ? Item.OverrideThumbPath : Item.SourceThumbPath;

	TSharedRef<SOverlay> Overlay = SNew(SOverlay);

	Overlay->AddSlot()
	[
		SNew(SBox)
		.WidthOverride(TileSize)
		.HeightOverride(TileSize)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			MakeThumbnail(MainPath, TileSize)
		]
	];

	// Pass-through dim + orphan tint
	if (State == EPCGExVariantTileState::PassThrough)
	{
		Overlay->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
		];
	}
	else if (State == EPCGExVariantTileState::Orphaned)
	{
		Overlay->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.5f, 0.05f, 0.05f, 0.5f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OrphanedTile", "Missing\nsource entry"))
				.Justification(ETextJustify::Center)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		];
	}

	// Source badge (bottom-left) when a replacement shows — the "before" in before→after.
	// For rule-definition tiles the "before" is the matched asset itself.
	if (bShowsReplacement)
	{
		Overlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(3.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.8f))
			.Padding(2.f)
			[
				SNew(SBox)
				.WidthOverride(BadgeSize)
				.HeightOverride(BadgeSize)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					MakeThumbnail(Item.SourceThumbPath, BadgeSize)
				]
			]
		];
	}

	// Actions (top-right): declare/specialize ("+") or revoke/delete ("×"), by state.
	if (State == EPCGExVariantTileState::PassThrough || State == EPCGExVariantTileState::SwappedByRule)
	{
		Overlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(3.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(State == EPCGExVariantTileState::SwappedByRule
				             ? LOCTEXT("SpecializeSwapTooltip", "Specialize: declare an explicit swap for this entry, overriding the asset rule")
				             : LOCTEXT("DeclareSwapTooltip", "Declare a swap for this entry -- choose the replacement's entry type, or start from a copy of the source entry"))
			.OnClicked_Lambda([this]() { OnDeclareSwap.ExecuteIfBound(ItemIndex); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Plus"))
			]
		];
	}
	else
	{
		Overlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(3.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(State == EPCGExVariantTileState::RuleDefinition
				             ? LOCTEXT("DeleteRuleTooltip", "Delete this asset swap rule (all entries it covers revert to pass-through)")
				             : LOCTEXT("RevokeSwapTooltip", "Remove this swap (the source entry passes through unchanged)"))
			.OnClicked_Lambda([this]() { OnRevokeSwap.ExecuteIfBound(ItemIndex); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.X"))
			]
		];
	}

	// Rule-coverage indicator (top-left) on tiles swapped via an asset rule.
	if (State == EPCGExVariantTileState::SwappedByRule)
	{
		Overlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(3.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.7f))
			.Padding(2.f)
			.ToolTipText(LOCTEXT("RuleCoveredTooltip", "Swapped by an asset rule (see the Asset Swaps group). Edits to the payload affect every entry the rule covers."))
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Link"))
			]
		];
	}

	// Label (bottom, full width)
	Overlay->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Bottom)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(STextBlock)
			.Text(Item.Label)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
	];

	// Grammar symbol badges (bottom-right, above the label bar) — same visibility rule as the
	// regular collection editor: hidden when the resolved symbol is None. Swapped tiles show the
	// payload's effective symbol; a payload without one displays the source's symbol instead
	// (DISPLAY-ONLY inheritance — runtime resolves the payload grammar as-is). When both are set
	// and differ, source stacks above variant, matching the tile's before/after language.
	FName PrimarySymbol = NAME_None;
	FLinearColor PrimaryColor = FLinearColor::White;
	FName SecondarySymbol = NAME_None; // the "before" symbol when both show
	FLinearColor SecondaryColor = FLinearColor::White;
	bool bInheritedDisplay = false;

	if (State == EPCGExVariantTileState::PassThrough)
	{
		PrimarySymbol = Item.SourceSymbol;
		PrimaryColor = Item.SourceSymbolColor;
	}
	else if (State == EPCGExVariantTileState::Swapped || State == EPCGExVariantTileState::SwappedByRule)
	{
		if (Item.OverrideSymbol.IsNone())
		{
			PrimarySymbol = Item.SourceSymbol;
			PrimaryColor = Item.SourceSymbolColor;
			bInheritedDisplay = !Item.SourceSymbol.IsNone();
		}
		else
		{
			PrimarySymbol = Item.OverrideSymbol;
			PrimaryColor = Item.OverrideSymbolColor;
			if (!Item.SourceSymbol.IsNone() && Item.SourceSymbol != Item.OverrideSymbol)
			{
				SecondarySymbol = Item.SourceSymbol;
				SecondaryColor = Item.SourceSymbolColor;
			}
		}
	}

	if (!PrimarySymbol.IsNone())
	{
		// Stacked source-over-variant (before above, after below) — long symbols truncate
		// when inlined side by side, and stacking needs no separator glyph.
		const TSharedRef<SVerticalBox> SymbolStack = SNew(SVerticalBox);

		if (!SecondarySymbol.IsNone())
		{
			SymbolStack->AddSlot()
			           .AutoHeight()
			           .HAlign(HAlign_Right)
			           .Padding(0.f, 0.f, 0.f, 2.f)
			[
				MakeSymbolBadge(SecondarySymbol, SecondaryColor, LOCTEXT("SourceSymbolTooltip", "Source entry symbol"))
			];
		}

		SymbolStack->AddSlot()
		           .AutoHeight()
		           .HAlign(HAlign_Right)
		[
			MakeSymbolBadge(
				PrimarySymbol, PrimaryColor,
				bInheritedDisplay
					? LOCTEXT("InheritedSymbolTooltip", "Symbol inherited from the source entry (the swap payload defines none)")
					: SecondarySymbol.IsNone()
					? FText::GetEmpty()
					: LOCTEXT("VariantSymbolTooltip", "Variant override symbol"))
		];

		Overlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(3.f, 0.f, 3.f, 22.f)) // bottom clearance for the label bar
		[
			SNew(SBox)
			.MaxDesiredWidth(TileSize - 6.f)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SymbolStack
			]
		];
	}

	RootBorder->SetContent(Overlay);
}

FReply SPCGExVariantGridTile::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnTileClicked.ExecuteIfBound(ItemIndex);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SPCGExVariantGridTile::MakeThumbnail(const FSoftObjectPath& AssetPath, const float InSize) const
{
	if (AssetPath.IsNull() || !ThumbnailPool.IsValid())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("?"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	TSharedPtr<FAssetThumbnail> Thumbnail;
	if (ThumbnailCachePtr)
	{
		if (const TSharedPtr<FAssetThumbnail>* Cached = ThumbnailCachePtr->Find(AssetPath))
		{
			Thumbnail = *Cached;
		}
	}

	if (!Thumbnail.IsValid())
	{
		const FAssetData AssetData = PCGExCollectionEditorUtils::ResolveEntryAssetData(AssetPath);
		Thumbnail = MakeShared<FAssetThumbnail>(AssetData, FMath::RoundToInt32(InSize), FMath::RoundToInt32(InSize), ThumbnailPool);
		if (ThumbnailCachePtr)
		{
			ThumbnailCachePtr->Add(AssetPath, Thumbnail);
		}
	}

	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.bAllowFadeIn = false;
	return Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
}

TSharedRef<SWidget> SPCGExVariantGridTile::MakeSymbolBadge(const FName Symbol, const FLinearColor& Color, const FText& Tooltip) const
{
	FLinearColor Bg = Color;
	Bg.A = 0.85f;

	return SNew(SBorder)
		.BorderImage(PCGExCollectionEditorSlateUtils::GetBadgeBrush())
		.BorderBackgroundColor(FSlateColor(Bg))
		.Padding(FMargin(3, 1))
		.ToolTipText(Tooltip)
		[
			SNew(STextBlock)
			.Text(FText::FromName(Symbol))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(PCGExCollectionEditorSlateUtils::PickReadableTextColor(Color))
		];
}

#pragma endregion

#pragma region SPCGExVariantGridView

void SPCGExVariantGridView::Construct(const FArguments& InArgs)
{
	Collection = InArgs._Collection;
	ThumbnailPool = InArgs._ThumbnailPool;
	TileSize = InArgs._TileSize;

	// Details pane
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = false;

	const FStructureDetailsViewArgs StructureViewArgs;

	StructDetailView = PropertyModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, nullptr);
	StructDetailView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SPCGExVariantGridView::OnDetailPropertyChanged);

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.7f)
		[
			SAssignNew(GroupScrollBox, SScrollBox)
			.Orientation(Orient_Vertical)
		]

		+ SSplitter::Slot()
		.Value(0.3f)
		[
			SAssignNew(DetailsHost, SBox)
			[
				StructDetailView->GetWidget().ToSharedRef()
			]
		]
	];

	TransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(this, &SPCGExVariantGridView::OnObjectTransacted);

	RefreshGrid();
}

SPCGExVariantGridView::~SPCGExVariantGridView()
{
	FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactedHandle);
}

void SPCGExVariantGridView::RefreshGrid(const bool bRefreshDetailPanel)
{
	// Preserve selection across rebuilds by identity, not index.
	int32 PrevGroup = INDEX_NONE;
	int32 PrevEntryId = 0;
	int32 PrevRuleIdx = INDEX_NONE;
	bool bPrevWasRule = false;
	if (Items.IsValidIndex(SelectedItem))
	{
		PrevGroup = Items[SelectedItem].GroupIdx;
		PrevEntryId = Items[SelectedItem].SourceEntryId;
		PrevRuleIdx = Items[SelectedItem].PathRuleIdx;
		bPrevWasRule = Items[SelectedItem].bIsRuleDefinition;
	}

	// Snapshot for structure comparison — payload edits and declare/revoke keep the tile
	// structure identical, and must not tear the layout down (flash + scroll jump).
	TArray<FPCGExVariantGridItem> OldItems = MoveTemp(Items);
	TArray<FName> OldGroupNames = MoveTemp(SortedGroupNames);

	RebuildItems();

	SelectedItem = INDEX_NONE;
	if (bPrevWasRule)
	{
		for (int32 i = 0; i < Items.Num(); i++)
		{
			if (Items[i].bIsRuleDefinition && Items[i].PathRuleIdx == PrevRuleIdx)
			{
				SelectedItem = i;
				break;
			}
		}
	}
	else if (PrevEntryId != 0)
	{
		for (int32 i = 0; i < Items.Num(); i++)
		{
			if (!Items[i].bIsRuleDefinition && Items[i].GroupIdx == PrevGroup && Items[i].SourceEntryId == PrevEntryId)
			{
				SelectedItem = i;
				break;
			}
		}
	}

	// Items are built per group in deterministic order, so identical group names + identical
	// flat item identities imply an identical layout structure.
	bool bSameStructure = OldGroupNames == SortedGroupNames && OldItems.Num() == Items.Num();
	if (bSameStructure)
	{
		for (int32 i = 0; i < Items.Num(); i++)
		{
			const FPCGExVariantGridItem& A = OldItems[i];
			const FPCGExVariantGridItem& B = Items[i];
			if (A.bIsRuleDefinition != B.bIsRuleDefinition
				|| A.GroupIdx != B.GroupIdx
				|| A.SourceRawIndex != B.SourceRawIndex
				|| A.SourceEntryId != B.SourceEntryId
				|| (A.bIsRuleDefinition && A.PathRuleIdx != B.PathRuleIdx))
			{
				bSameStructure = false;
				break;
			}
		}
	}

	if (bSameStructure)
	{
		// In-place: tiles persist (and so does scroll position); only changed content rebuilds.
		for (const TPair<int32, TSharedPtr<SPCGExVariantGridTile>>& Pair : ActiveTiles)
		{
			if (Pair.Value.IsValid() && Items.IsValidIndex(Pair.Key))
			{
				Pair.Value->UpdateItem(Items[Pair.Key]);
			}
		}
		ApplySelectionVisuals();
	}
	else
	{
		RebuildLayout();
	}

	if (bRefreshDetailPanel)
	{
		UpdateDetailForSelection();
	}
}

void SPCGExVariantGridView::RebuildItems()
{
	Items.Reset();
	SortedGroupNames.Reset();
	GroupToItems.Reset();

	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant)
	{
		return;
	}

	// Asset-path rules (matching key mirrors the bake: Staging.Path, first rule wins).
	TMap<FSoftObjectPath, int32> PathToRule;
	PathToRule.Reserve(Variant->PathOverrides.Num());
	for (int32 r = 0; r < Variant->PathOverrides.Num(); r++)
	{
		const FPCGExVariantPathOverride& Rule = Variant->PathOverrides[r];
		if (!Rule.MatchAsset.IsNull() && Rule.Entry.IsValid() && !PathToRule.Contains(Rule.MatchAsset))
		{
			PathToRule.Add(Rule.MatchAsset, r);
		}
	}

	for (int32 GroupIdx = 0; GroupIdx < Variant->Sources.Num(); GroupIdx++)
	{
		FPCGExVariantSource& Group = Variant->Sources[GroupIdx];
		const FName GroupName = PCGExVariantGrid::MakeGroupName(Group.Source, GroupIdx);

		SortedGroupNames.Add(GroupName);
		TArray<int32>& GroupItems = GroupToItems.FindOrAdd(GroupName);

		if (Group.Source.IsNull())
		{
			continue;
		}

		PCGExHelpers::LoadBlocking_AnyThreadTpl(Group.Source);
		UPCGExAssetCollection* Src = Group.Source.Get();
		if (!Src)
		{
			continue;
		}

		// A never-rebuilt legacy source has no EntryIds — assign them now so tiles are bindable.
		// Scoped to id assignment only (no staging rebuild); dirties the source once.
		bool bAnyMissingId = false;
		Src->ForEachEntry([&bAnyMissingId](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			bAnyMissingId |= Entry->EntryId == 0;
		});
		if (bAnyMissingId)
		{
			Src->Modify();
			Src->SyncEntryIds();
		}

		// EntryId -> override row for this group
		TMap<int32, int32> IdToRow;
		IdToRow.Reserve(Group.Overrides.Num());
		for (int32 r = 0; r < Group.Overrides.Num(); r++)
		{
			if (Group.Overrides[r].SourceEntryId != 0)
			{
				IdToRow.Add(Group.Overrides[r].SourceEntryId, r);
			}
		}

		TSet<int32> MatchedRows;

		Src->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, const int32 RawIndex)
		{
			FPCGExVariantGridItem& NewItem = Items.Emplace_GetRef();
			NewItem.GroupIdx = GroupIdx;
			NewItem.SourceRawIndex = RawIndex;
			NewItem.SourceEntryId = Entry->EntryId;
			NewItem.SourceThumbPath = Entry->EDITOR_GetThumbnailAssetPath();
			NewItem.Label = FText::FromString(NewItem.SourceThumbPath.GetAssetName());

			if (const FPCGExAssetGrammarDetails* Grammar = Entry->GetEffectiveGrammar(Src))
			{
				NewItem.SourceSymbol = Grammar->Symbol;
				NewItem.SourceSymbolColor = Grammar->DebugColor;
			}

			if (const int32* Row = IdToRow.Find(Entry->EntryId))
			{
				NewItem.OverrideRowIdx = *Row;
				MatchedRows.Add(*Row);
				if (const FPCGExAssetCollectionEntry* Payload = Group.Overrides[*Row].Entry.GetPtr<FPCGExAssetCollectionEntry>())
				{
					NewItem.OverrideThumbPath = Payload->EDITOR_GetThumbnailAssetPath();
					if (const FPCGExAssetGrammarDetails* Grammar = Payload->GetEffectiveGrammar(Variant))
					{
						NewItem.OverrideSymbol = Grammar->Symbol;
						NewItem.OverrideSymbolColor = Grammar->DebugColor;
					}
				}
			}
			else if (!Entry->bIsSubCollection)
			{
				// No explicit row — an asset rule may cover this entry (effective-state display).
				if (const int32* Rule = PathToRule.Find(Entry->Staging.Path))
				{
					NewItem.PathRuleIdx = *Rule;
					if (const FPCGExAssetCollectionEntry* Payload = Variant->PathOverrides[*Rule].Entry.GetPtr<FPCGExAssetCollectionEntry>())
					{
						NewItem.OverrideThumbPath = Payload->EDITOR_GetThumbnailAssetPath();
						if (const FPCGExAssetGrammarDetails* Grammar = Payload->GetEffectiveGrammar(Variant))
						{
							NewItem.OverrideSymbol = Grammar->Symbol;
							NewItem.OverrideSymbolColor = Grammar->DebugColor;
						}
					}
				}
			}

			GroupItems.Add(Items.Num() - 1);
		});

		// Orphaned rows: declared swaps whose source entry no longer exists.
		for (int32 r = 0; r < Group.Overrides.Num(); r++)
		{
			if (Group.Overrides[r].SourceEntryId == 0 || MatchedRows.Contains(r))
			{
				continue;
			}

			FPCGExVariantGridItem& Orphan = Items.Emplace_GetRef();
			Orphan.GroupIdx = GroupIdx;
			Orphan.SourceEntryId = Group.Overrides[r].SourceEntryId;
			Orphan.OverrideRowIdx = r;
			if (const FPCGExAssetCollectionEntry* Payload = Group.Overrides[r].Entry.GetPtr<FPCGExAssetCollectionEntry>())
			{
				Orphan.OverrideThumbPath = Payload->EDITOR_GetThumbnailAssetPath();
				Orphan.SourceThumbPath = Orphan.OverrideThumbPath;
			}
			Orphan.Label = LOCTEXT("OrphanLabel", "Orphaned swap");

			GroupItems.Add(Items.Num() - 1);
		}
	}

	// Synthetic group: one definition tile per asset-path rule (including inert ones, so
	// half-authored rules stay visible and fixable).
	if (!Variant->PathOverrides.IsEmpty())
	{
		SortedGroupNames.Add(PCGExVariantGrid::AssetSwapsGroupName);
		TArray<int32>& RuleItems = GroupToItems.FindOrAdd(PCGExVariantGrid::AssetSwapsGroupName);

		for (int32 r = 0; r < Variant->PathOverrides.Num(); r++)
		{
			const FPCGExVariantPathOverride& Rule = Variant->PathOverrides[r];

			FPCGExVariantGridItem& RuleItem = Items.Emplace_GetRef();
			RuleItem.bIsRuleDefinition = true;
			RuleItem.PathRuleIdx = r;
			RuleItem.SourceThumbPath = Rule.MatchAsset;
			if (const FPCGExAssetCollectionEntry* Payload = Rule.Entry.GetPtr<FPCGExAssetCollectionEntry>())
			{
				RuleItem.OverrideThumbPath = Payload->EDITOR_GetThumbnailAssetPath();
			}
			RuleItem.Label = Rule.MatchAsset.IsNull()
				                 ? LOCTEXT("UnsetRuleLabel", "Unset rule")
				                 : FText::FromString(Rule.MatchAsset.GetAssetName());

			RuleItems.Add(Items.Num() - 1);
		}
	}
}

void SPCGExVariantGridView::RebuildLayout()
{
	GroupScrollBox->ClearChildren();
	GroupWidgets.Reset();
	ActiveTiles.Reset();

	for (const FName& GroupName : SortedGroupNames)
	{
		const TArray<int32>* GroupItems = GroupToItems.Find(GroupName);
		const int32 Count = GroupItems ? GroupItems->Num() : 0;
		const bool bCollapsed = CollapsedGroups.Contains(GroupName);

		TSharedRef<SPCGExCollectionCategoryGroup> GroupWidget =
			SNew(SPCGExCollectionCategoryGroup)
			.CategoryName(GroupName)
			.EntryCount(Count)
			.bIsCollapsed(bCollapsed)
			.bAllowRename(false) // group names mirror source asset names — never editable here
			// Group widgets swallow asset drops (Handled); route them to the shared add-source path.
			.OnAssetDropOnCategory_Lambda([this](const FName, const TArray<FAssetData>& InAssets)
			{
				AddSourcesFromAssets(InAssets);
			})
			.OnExpansionChanged_Lambda([this](const FName InGroup, const bool bIsExpanded)
			{
				if (bIsExpanded)
				{
					CollapsedGroups.Remove(InGroup);
					PopulateGroupTiles(InGroup);
				}
				else
				{
					CollapsedGroups.Add(InGroup);
				}
			});

		GroupWidgets.Add(GroupName, GroupWidget);
		GroupScrollBox->AddSlot()
		[
			GroupWidget
		];

		if (!bCollapsed)
		{
			PopulateGroupTiles(GroupName);
		}
	}

	ApplySelectionVisuals();
}

void SPCGExVariantGridView::PopulateGroupTiles(const FName GroupName)
{
	const TSharedPtr<SPCGExCollectionCategoryGroup>* GroupWidget = GroupWidgets.Find(GroupName);
	const TArray<int32>* GroupItems = GroupToItems.Find(GroupName);
	if (!GroupWidget || !GroupWidget->IsValid() || !GroupItems)
	{
		return;
	}

	(*GroupWidget)->ClearTiles();

	for (const int32 ItemIdx : *GroupItems)
	{
		TSharedRef<SPCGExVariantGridTile> Tile =
			SNew(SPCGExVariantGridTile)
			.TileSize(TileSize)
			.ItemIndex(ItemIdx)
			.Item(Items[ItemIdx])
			.ThumbnailPool(ThumbnailPool)
			.ThumbnailCachePtr(&ThumbnailCache)
			.OnTileClicked_Raw(this, &SPCGExVariantGridView::OnTileClicked)
			.OnDeclareSwap_Raw(this, &SPCGExVariantGridView::DeclareSwap)
			.OnRevokeSwap_Raw(this, &SPCGExVariantGridView::RevokeSwap);

		ActiveTiles.Add(ItemIdx, Tile);
		(*GroupWidget)->AddTile(Tile);
	}

	ApplySelectionVisuals();
}

void SPCGExVariantGridView::OnTileClicked(const int32 ItemIndex)
{
	SelectedItem = ItemIndex;
	ApplySelectionVisuals();
	UpdateDetailForSelection();
}

void SPCGExVariantGridView::DeclareSwap(const int32 ItemIndex)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	const FPCGExVariantGridItem& Item = Items[ItemIndex];
	const EPCGExVariantTileState State = Item.GetState();

	// Declare on pass-through, or specialize a rule-covered entry (explicit rows take
	// precedence over rules at bake time, so this cleanly overrides the rule per-entry).
	if ((State != EPCGExVariantTileState::PassThrough && State != EPCGExVariantTileState::SwappedByRule)
		|| !Variant->Sources.IsValidIndex(Item.GroupIdx))
	{
		return;
	}

	UPCGExAssetCollection* Src = Variant->Sources[Item.GroupIdx].Source.Get();
	if (!Src)
	{
		return;
	}

	const FPCGExEntryAccessResult SourceEntry = Src->GetEntryRaw(Item.SourceRawIndex);
	if (!SourceEntry.IsValid())
	{
		return;
	}

	// Type chooser at the cursor -- same picker the heterogeneous "+ Add" uses. First item
	// keeps the legacy one-click behavior (full copy of the source entry); the rest start
	// the replacement as a fresh payload of the chosen type. Row creation re-validates, so
	// a stale menu (undo while open) degrades to a no-op.
	const FText SourceTypeLabel = PCGExCollectionEditorUtils::GetEntryTypeLabel(
		PCGExAssetCollection::FTypeRegistry::Get().GetEntryStruct(SourceEntry.Entry->GetTypeId()));

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("DeclareSwapCopySection", "Declare Swap"));
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("DeclareSwapCopy", "Copy Source Entry ({0})"), SourceTypeLabel),
		LOCTEXT("DeclareSwapCopyTooltip", "The replacement starts as a full copy of the source entry -- swap-the-asset becomes a one-field edit; weights, variations and tags carry over."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSPLambda(this, [this, ItemIndex]()
		{
			DeclareSwapAs(ItemIndex, nullptr);
		})));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("DeclareSwapTypeSection", "Swap With Type"));
	TArray<const UScriptStruct*> EntryTypes;
	PCGExCollectionEditorUtils::GetAllConcreteEntryTypes(EntryTypes);
	for (const UScriptStruct* EntryStruct : EntryTypes)
	{
		const FText Label = PCGExCollectionEditorUtils::GetEntryTypeLabel(EntryStruct);
		MenuBuilder.AddMenuEntry(
			Label,
			FText::Format(LOCTEXT("DeclareSwapTypeTooltip", "The replacement starts as a fresh {0} entry; the source's weight, category, tags and variations carry over."), Label),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(this, [this, ItemIndex, EntryStruct]()
			{
				DeclareSwapAs(ItemIndex, EntryStruct);
			})));
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void SPCGExVariantGridView::DeclareSwapAs(const int32 ItemIndex, const UScriptStruct* EntryStruct)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	const FPCGExVariantGridItem& Item = Items[ItemIndex];
	const EPCGExVariantTileState State = Item.GetState();

	if ((State != EPCGExVariantTileState::PassThrough && State != EPCGExVariantTileState::SwappedByRule)
		|| !Variant->Sources.IsValidIndex(Item.GroupIdx))
	{
		return;
	}

	FPCGExVariantSource& Group = Variant->Sources[Item.GroupIdx];
	UPCGExAssetCollection* Src = Group.Source.Get();
	if (!Src)
	{
		return;
	}

	// Null EntryStruct = full copy of the source entry. Struct type resolved from the
	// ENTRY's own type id (not the host class): hosts may be heterogeneous — notably another
	// variant collection, which is a legal source for daisy-chained swap nodes.
	const FPCGExEntryAccessResult SourceEntry = Src->GetEntryRaw(Item.SourceRawIndex);
	const bool bCopySource = EntryStruct == nullptr;
	if (bCopySource)
	{
		EntryStruct = SourceEntry.IsValid()
			              ? PCGExAssetCollection::FTypeRegistry::Get().GetEntryStruct(SourceEntry.Entry->GetTypeId())
			              : nullptr;
	}

	if (!EntryStruct || !SourceEntry.IsValid())
	{
		return;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("DeclareSwap", "Declare Entry Swap"));
		bIsSyncing = true;
		Variant->Modify();

		FPCGExVariantEntryOverride& NewRow = Group.Overrides.AddDefaulted_GetRef();
		NewRow.SourceEntryId = Item.SourceEntryId;

		if (bCopySource)
		{
			// Full copy — swap-the-asset becomes a one-field edit and everything carries over.
			NewRow.Entry.InitializeAs(EntryStruct, reinterpret_cast<const uint8*>(SourceEntry.Entry));
		}
		else
		{
			// Fresh payload of the chosen type; carry the source's BASE fields over
			// (weight/category/tags/variations, staging until the next rebuild) so the swap
			// slots into the same pick distribution. Base offsets are shared across derived
			// entry structs, so a base-struct copy into the derived payload is well-defined
			// (same reasoning as the globals seam's derived-block copy-out).
			NewRow.Entry.InitializeAs(EntryStruct);
			FPCGExAssetCollectionEntry::StaticStruct()->CopyScriptStruct(
				NewRow.Entry.GetMutableMemory(), SourceEntry.Entry, 1);
		}

		// Cross-ASSET copy: any Instanced subobject refs the payload carried (e.g. a PCGData
		// entry's ExportedDataAsset) are SHALLOW -- duplicate them into the variant, per the
		// standing rule for every cross-asset copy of an entry payload. No-op for payloads
		// without instanced refs.
		PCGExCollectionHelpers::DuplicateInstancedSubobjects(EntryStruct, NewRow.Entry.GetMutableMemory(), Variant);

		// The copy carried the SOURCE entry's identity; zero it so the variant's own
		// SyncEntryIds assigns a fresh one on the next staging rebuild. Also bake the
		// source collection's Global channels into the payload — the variant host cannot
		// provide typed globals (ISM/skinned descriptors), they'd be silently lost.
		// (For a cross-type payload this resolves the CHOSEN type's channels against the
		// source host — a no-op unless the source actually provides globals for that type.)
		if (FPCGExAssetCollectionEntry* Payload = NewRow.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>())
		{
			Payload->EntryId = 0;
			Payload->ResolveGlobalsToLocal(Src);
		}

		Variant->PostEditChange();
		bIsSyncing = false;
	}

	SelectedItem = ItemIndex;
	RefreshGrid();
}

void SPCGExVariantGridView::RevokeSwap(const int32 ItemIndex)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	const FPCGExVariantGridItem& Item = Items[ItemIndex];

	if (Item.bIsRuleDefinition)
	{
		if (!Variant->PathOverrides.IsValidIndex(Item.PathRuleIdx))
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("DeleteRule", "Delete Asset Swap Rule"));
		bIsSyncing = true;
		Variant->Modify();
		Variant->PathOverrides.RemoveAt(Item.PathRuleIdx);
		Variant->PostEditChange();
		bIsSyncing = false;

		RefreshGrid();
		return;
	}

	if (Item.OverrideRowIdx == INDEX_NONE || !Variant->Sources.IsValidIndex(Item.GroupIdx))
	{
		return;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("RevokeSwap", "Remove Entry Swap"));
		bIsSyncing = true;
		Variant->Modify();
		Variant->Sources[Item.GroupIdx].Overrides.RemoveAt(Item.OverrideRowIdx);
		Variant->PostEditChange();
		bIsSyncing = false;
	}

	RefreshGrid();
}

void SPCGExVariantGridView::ApplySelectionVisuals()
{
	for (const TPair<int32, TSharedPtr<SPCGExVariantGridTile>>& Pair : ActiveTiles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->SetSelected(Pair.Key == SelectedItem);
		}
	}
}

void SPCGExVariantGridView::UpdateDetailForSelection()
{
	CurrentStructScope.Reset();

	UScriptStruct* PayloadStruct = nullptr;
	uint8* PayloadMemory = nullptr;
	bool bEditable = false;

	if (Items.IsValidIndex(SelectedItem))
	{
		const FPCGExVariantGridItem& Item = Items[SelectedItem];

		if (ResolveOverridePayload(Item, PayloadStruct, PayloadMemory))
		{
			bEditable = true;
		}
		else if (Item.GetState() == EPCGExVariantTileState::PassThrough)
		{
			// Read-only view of the source entry for inspection.
			if (const UPCGExVariantCollection* Variant = Collection.Get();
				Variant && Variant->Sources.IsValidIndex(Item.GroupIdx))
			{
				if (UPCGExAssetCollection* Src = Variant->Sources[Item.GroupIdx].Source.Get())
				{
					PCGExAssetCollection::FTypeInfo TypeInfo;
					const bool bFound = PCGExAssetCollection::FTypeRegistry::Get().GetInfoByClass(Src->GetClass(), TypeInfo);
					const FPCGExEntryAccessResult SourceEntry = Src->GetEntryRaw(Item.SourceRawIndex);
					if (bFound && TypeInfo.EntryStruct && SourceEntry.IsValid())
					{
						PayloadStruct = TypeInfo.EntryStruct;
						PayloadMemory = const_cast<uint8*>(reinterpret_cast<const uint8*>(SourceEntry.Entry));
					}
				}
			}
		}
	}

	if (PayloadStruct && PayloadMemory)
	{
		CurrentStructScope = MakeShared<FStructOnScope>(PayloadStruct);
		PayloadStruct->CopyScriptStruct(CurrentStructScope->GetStructMemory(), PayloadMemory);
		StructDetailView->SetStructureData(CurrentStructScope);
	}
	else
	{
		StructDetailView->SetStructureData(nullptr);
	}

	DetailsHost->SetEnabled(bEditable);
}

void SPCGExVariantGridView::OnDetailPropertyChanged(const FPropertyChangedEvent& Event)
{
	if (bIsSyncing || !Items.IsValidIndex(SelectedItem) || !CurrentStructScope.IsValid())
	{
		return;
	}

	UPCGExVariantCollection* Variant = Collection.Get();
	UScriptStruct* PayloadStruct = nullptr;
	uint8* PayloadMemory = nullptr;

	if (!Variant || !ResolveOverridePayload(Items[SelectedItem], PayloadStruct, PayloadMemory))
	{
		return;
	}

	if (PayloadStruct != CurrentStructScope->GetStruct())
	{
		return;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("EditSwapEntry", "Edit Swap Entry"));
		bIsSyncing = true;
		Variant->Modify();
		PayloadStruct->CopyScriptStruct(PayloadMemory, CurrentStructScope->GetStructMemory());
		Variant->PostEditChange();
		bIsSyncing = false;
	}

	// Thumbnail may have changed with the asset — refresh tiles in place; the details pane
	// already holds exactly what was typed, don't rebuild it under the user's cursor.
	RefreshGrid(/*bRefreshDetailPanel=*/ false);
}

bool SPCGExVariantGridView::ResolveOverridePayload(const FPCGExVariantGridItem& InItem, UScriptStruct*& OutStruct, uint8*& OutMemory) const
{
	OutStruct = nullptr;
	OutMemory = nullptr;

	UPCGExVariantCollection* Variant = const_cast<UPCGExVariantCollection*>(Collection.Get());
	if (!Variant)
	{
		return false;
	}

	FInstancedStruct* PayloadPtr = nullptr;

	if (InItem.OverrideRowIdx != INDEX_NONE && Variant->Sources.IsValidIndex(InItem.GroupIdx))
	{
		FPCGExVariantSource& Group = Variant->Sources[InItem.GroupIdx];
		if (Group.Overrides.IsValidIndex(InItem.OverrideRowIdx))
		{
			PayloadPtr = &Group.Overrides[InItem.OverrideRowIdx].Entry;
		}
	}
	else if (InItem.PathRuleIdx != INDEX_NONE && Variant->PathOverrides.IsValidIndex(InItem.PathRuleIdx))
	{
		// Rule payload — selected via a rule-definition tile or a rule-covered entry tile.
		// Edits affect every entry the rule covers.
		PayloadPtr = &Variant->PathOverrides[InItem.PathRuleIdx].Entry;
	}

	if (!PayloadPtr || !PayloadPtr->IsValid())
	{
		return false;
	}

	FInstancedStruct& Payload = *PayloadPtr;

	OutStruct = const_cast<UScriptStruct*>(Cast<const UScriptStruct>(Payload.GetScriptStruct()));
	OutMemory = Payload.GetMutableMemory();
	return OutStruct && OutMemory;
}

void SPCGExVariantGridView::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (bIsSyncing || Object != Collection.Get())
	{
		return;
	}
	RefreshGrid();
}

int32 SPCGExVariantGridView::AddSourcesFromAssets(const TArray<FAssetData>& InAssets)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant)
	{
		return 0;
	}

	int32 NumAlreadyDeclared = 0;
	TArray<UPCGExAssetCollection*> ToAdd;

	for (const FAssetData& Asset : InAssets)
	{
		if (!PCGExVariantGrid::CanBeSource(Asset, Variant))
		{
			continue;
		}

		UPCGExAssetCollection* Source = Cast<UPCGExAssetCollection>(Asset.GetAsset());
		if (!Source)
		{
			continue;
		}

		if (Variant->FindSourceGroup(FSoftObjectPath(Source)) || ToAdd.Contains(Source))
		{
			NumAlreadyDeclared++;
			continue;
		}

		ToAdd.Add(Source);
	}

	if (!ToAdd.IsEmpty())
	{
		{
			FScopedTransaction Transaction(LOCTEXT("AddSources", "Add Variant Sources"));
			bIsSyncing = true;
			Variant->Modify();
			for (UPCGExAssetCollection* Source : ToAdd)
			{
				Variant->Sources.AddDefaulted_GetRef().Source = Source;
			}
			Variant->PostEditChange();
			bIsSyncing = false;
		}

		RefreshGrid();
	}

	if (NumAlreadyDeclared > 0)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SourcesAlreadyDeclaredFmt", "{0} {0}|plural(one=collection is,other=collections are) already declared as a source of this variant."),
			NumAlreadyDeclared));
		Info.ExpireDuration = 3.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return ToAdd.Num() + NumAlreadyDeclared;
}

FReply SPCGExVariantGridView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		for (const FAssetData& Asset : AssetOp->GetAssets())
		{
			if (PCGExVariantGrid::CanBeSource(Asset, Collection.Get()))
			{
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

FReply SPCGExVariantGridView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		// Handled whenever the op carried candidate collections -- even all-duplicates
		// (the skip toast is the drop's outcome).
		if (AddSourcesFromAssets(AssetOp->GetAssets()) > 0)
		{
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
