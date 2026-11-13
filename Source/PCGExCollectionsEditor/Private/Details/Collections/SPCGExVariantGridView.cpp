// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExVariantGridView.h"

#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ITransaction.h"
#include "Details/Collections/PCGExGenericAssetPickerCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Details/Collections/PCGExCollectionEditorSlateUtils.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/PCGExCollectionCategoryGroups.h"
#include "Details/Collections/SPCGExCollectionCategoryGroup.h"
#include "Helpers/PCGExMaterialSwapTable.h"

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
	inline FName MakeGroupName(const UPCGExAssetCollection* Source, const int32 GroupIdx)
	{
		if (!Source)
		{
			return FName(*FString::Printf(TEXT("Unset Source [%d]"), GroupIdx));
		}
		return FName(*Source->GetName());
	}

	// Synthetic group hosting the asset-path swap rules.
	inline const FName AssetSwapsGroupName = FName(TEXT("Asset Swaps"));

	// Synthetic group hosting the material swap rows, in priority order.
	inline const FName MaterialSwapsGroupName = FName(TEXT("Material Swaps"));

	// Any collection subtype except the variant itself (other variants ARE legal --
	// daisy-chained swap nodes). Registry-only class check, no asset load.
	inline bool CanBeSource(const FAssetData& InAsset, const UPCGExVariantCollection* InVariant)
	{
		return InVariant
			&& InAsset.IsInstanceOf(UPCGExAssetCollection::StaticClass())
			&& InAsset.GetSoftObjectPath() != FSoftObjectPath(InVariant);
	}

	// Identity = same underlying row/entry; display fields may differ (tiles update in place).
	inline bool SameIdentity(const FPCGExVariantGridItem& A, const FPCGExVariantGridItem& B)
	{
		return A.bIsRuleDefinition == B.bIsRuleDefinition
			&& A.MaterialSwapIdx == B.MaterialSwapIdx
			&& A.GroupIdx == B.GroupIdx
			&& A.SourceRawIndex == B.SourceRawIndex
			&& A.SourceEntryId == B.SourceEntryId
			&& (!A.bIsRuleDefinition || A.PathRuleIdx == B.PathRuleIdx);
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
	OnDeleteRule = InArgs._OnDeleteRule;

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

void SPCGExVariantGridTile::UpdateItem(const int32 InItemIndex, const FPCGExVariantGridItem& InItem)
{
	ItemIndex = InItemIndex;

	const bool bDisplayChanged =
		Item.GetState() != InItem.GetState()
		|| Item.SourceThumbPath != InItem.SourceThumbPath
		|| Item.OverrideThumbPath != InItem.OverrideThumbPath
		|| !Item.Label.EqualTo(InItem.Label)
		|| Item.SourceSymbol != InItem.SourceSymbol
		|| Item.OverrideSymbol != InItem.OverrideSymbol
		|| Item.SourceSymbolColor != InItem.SourceSymbolColor
		|| Item.OverrideSymbolColor != InItem.OverrideSymbolColor
		|| Item.bMaterialSwapGated != InItem.bMaterialSwapGated
		|| Item.bMaterialSwapUnreachable != InItem.bMaterialSwapUnreachable
		|| Item.bMaterialSwapMatchUnset != InItem.bMaterialSwapMatchUnset;

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
		State == EPCGExVariantTileState::RuleDefinition ||
		State == EPCGExVariantTileState::MaterialSwap;
	const FSoftObjectPath& MainPath = bShowsReplacement ? Item.OverrideThumbPath : Item.SourceThumbPath;

	TSharedRef<SOverlay> Overlay = SNew(SOverlay);

	// Red tint with a centred caption: a tile that can't apply as authored.
	auto AddAlert = [&Overlay](const FText& InCaption, const FText& InTooltip)
	{
		const TSharedRef<SBorder> Alert = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.5f, 0.05f, 0.05f, 0.5f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InCaption)
				.Justification(ETextJustify::Center)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			];

		if (!InTooltip.IsEmpty())
		{
			Alert->SetToolTipText(InTooltip);
		}

		Overlay->AddSlot()
		[
			Alert
		];
	};

	// Indicator icon in the top-left corner.
	auto AddCornerIcon = [&Overlay](const FName InBrush, const FText& InTooltip)
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
			.ToolTipText(InTooltip)
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush(InBrush))
			]
		];
	};

	Overlay->AddSlot()
	[
		MakeThumbnail(MainPath)
	];

	// Pass-through/unset dim + orphan tint
	if (State == EPCGExVariantTileState::PassThrough || State == EPCGExVariantTileState::RuleUnset || State == EPCGExVariantTileState::MaterialSwapUnset)
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
		AddAlert(LOCTEXT("OrphanedTile", "Missing\nsource entry"), FText::GetEmpty());
	}

	// The table ignores a row without a match, and drops the rows the Swap node warns about as unreachable.
	const FText NeverApplies = LOCTEXT("UnreachableMaterialSwapTile", "Never\napplies");
	if (Item.bMaterialSwapMatchUnset)
	{
		AddAlert(NeverApplies, LOCTEXT("UnmatchedMaterialSwapTooltip", "No material to match: set Match Material in the details panel."));
	}
	else if (Item.bMaterialSwapUnreachable)
	{
		AddAlert(NeverApplies, LOCTEXT("UnreachableMaterialSwapTooltip", "An earlier row for the same material always wins over this one (it has no tag filter, or the same one), so this row never applies."));
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
					MakeThumbnail(Item.SourceThumbPath)
				]
			]
		];
	}

	// Actions (top-right): declare/specialize ("+") or revoke ("x"), by state. Rule and material swap tiles
	// prepend a delete button -- revoke only clears the payload / replacement, delete removes the whole row.
	const TSharedRef<SHorizontalBox> ActionsBox = SNew(SHorizontalBox);
	const bool bIsMaterialSwap = Item.MaterialSwapIdx != INDEX_NONE;

	if (Item.bIsRuleDefinition || bIsMaterialSwap)
	{
		ActionsBox->AddSlot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(bIsMaterialSwap
				             ? LOCTEXT("DeleteMaterialSwapTooltip", "Delete this material swap row")
				             : LOCTEXT("DeleteRuleTooltip", "Delete this asset swap rule -- the asset reference and its payload are removed from the collection"))
			.OnClicked_Lambda([this]() { OnDeleteRule.ExecuteIfBound(ItemIndex); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Delete"))
			]
		];
	}

	if (State == EPCGExVariantTileState::PassThrough || State == EPCGExVariantTileState::SwappedByRule
		|| State == EPCGExVariantTileState::RuleUnset || State == EPCGExVariantTileState::MaterialSwapUnset)
	{
		FText DeclareTooltip = LOCTEXT("DeclareSwapTooltip", "Declare a swap for this entry -- choose the replacement's entry type, or start from a copy of the source entry");
		if (State == EPCGExVariantTileState::SwappedByRule)
		{
			DeclareTooltip = LOCTEXT("SpecializeSwapTooltip", "Specialize: declare an explicit swap for this entry, overriding the asset rule");
		}
		else if (State == EPCGExVariantTileState::RuleUnset)
		{
			DeclareTooltip = LOCTEXT("DeclareRuleSwapTooltip", "Declare what this asset swaps to -- choose the replacement's entry type, or start from a copy of a source entry staging the asset");
		}
		else if (State == EPCGExVariantTileState::MaterialSwapUnset)
		{
			DeclareTooltip = LOCTEXT("PickMaterialReplacementTooltip", "Pick the material rendered instead");
		}

		ActionsBox->AddSlot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(DeclareTooltip)
			.OnClicked_Lambda([this]() { OnDeclareSwap.ExecuteIfBound(ItemIndex); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Plus"))
			]
		];
	}
	else
	{
		FText RevokeTooltip = LOCTEXT("RevokeSwapTooltip", "Remove this swap (the source entry passes through unchanged)");
		if (State == EPCGExVariantTileState::RuleDefinition)
		{
			RevokeTooltip = LOCTEXT("RevokeRuleSwapTooltip", "Remove this rule's swap payload -- the asset reference stays as an unswapped rule; covered entries revert to pass-through");
		}
		else if (State == EPCGExVariantTileState::MaterialSwap)
		{
			RevokeTooltip = LOCTEXT("ClearMaterialReplacementTooltip", "Clear this row's replacement -- the row stays, unset, and swaps nothing");
		}

		ActionsBox->AddSlot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(RevokeTooltip)
			.OnClicked_Lambda([this]() { OnRevokeSwap.ExecuteIfBound(ItemIndex); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.X"))
			]
		];
	}

	Overlay->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(3.f)
	[
		ActionsBox
	];

	// Rule coverage on tiles swapped via an asset rule; tag filter on gated material swap rows.
	if (State == EPCGExVariantTileState::SwappedByRule)
	{
		AddCornerIcon(FName("Icons.Link"), LOCTEXT("RuleCoveredTooltip", "Swapped by an asset rule (see the Asset Swaps group). Edits to the payload affect every entry the rule covers."));
	}
	else if (Item.bMaterialSwapGated)
	{
		AddCornerIcon(FName("Icons.Filter"), LOCTEXT("GatedMaterialSwapTooltip", "Applies only to entries whose tags pass this row's tag filter (edit it in the details panel)."));
	}

	// Label (bottom, full width), with the full text as tooltip since it ellipsizes.
	Overlay->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Bottom)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		.Padding(FMargin(4.f, 2.f))
		.ToolTipText(Item.Label)
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

	// Fixed size: overlay content (the label above all) ellipsizes or clips, it never widens the tile.
	RootBorder->SetContent(
		SNew(SBox)
		.WidthOverride(TileSize)
		.HeightOverride(TileSize)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			Overlay
		]);
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

TSharedRef<SWidget> SPCGExVariantGridTile::MakeThumbnail(const FSoftObjectPath& AssetPath) const
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
		// Tile size whatever the slot: the cache is keyed by path, so a badge-sized render would be reused upscaled.
		const FAssetData AssetData = PCGExCollectionEditorUtils::ResolveEntryAssetData(AssetPath);
		Thumbnail = MakeShared<FAssetThumbnail>(AssetData, FMath::RoundToInt32(TileSize), FMath::RoundToInt32(TileSize), ThumbnailPool);
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

	// A structure view never runs the root entry's customization; property-level pickers go here.
	if (IDetailsView* InnerDetailsView = StructDetailView->GetDetailsView())
	{
		PCGExGenericAssetPicker::RegisterOnDetailsView(*InnerDetailsView);
		InnerDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateStatic(&PCGExCollectionCategoryGroups::IsPropertyVisible));
		InnerDetailsView->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateStatic(&PCGExCollectionCategoryGroups::IsCustomRowVisible));
	}

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
	int32 PrevMaterialSwapIdx = INDEX_NONE;
	bool bPrevWasRule = false;
	if (Items.IsValidIndex(SelectedItem))
	{
		PrevGroup = Items[SelectedItem].GroupIdx;
		PrevEntryId = Items[SelectedItem].SourceEntryId;
		PrevRuleIdx = Items[SelectedItem].PathRuleIdx;
		PrevMaterialSwapIdx = Items[SelectedItem].MaterialSwapIdx;
		bPrevWasRule = Items[SelectedItem].bIsRuleDefinition;
	}

	// Snapshot the old model: ReconcileLayout diffs against it per group, reusing every
	// widget whose group membership is unchanged.
	TArray<FPCGExVariantGridItem> OldItems = MoveTemp(Items);
	TArray<FName> OldGroupNames = MoveTemp(SortedGroupNames);
	TMap<FName, TArray<int32>> OldGroupToItems = MoveTemp(GroupToItems);

	RebuildItems();

	SelectedItem = INDEX_NONE;
	if (PrevMaterialSwapIdx != INDEX_NONE)
	{
		SelectedItem = FindMaterialSwapItem(PrevMaterialSwapIdx);
	}
	else if (bPrevWasRule)
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

	ReconcileLayout(OldItems, OldGroupNames, OldGroupToItems);

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

	// Asset-path rules (matching key mirrors BuildGroupMapping: Staging.Path, first rule wins).
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
		const FName GroupName = PCGExVariantGrid::MakeGroupName(Group.SourceCollection, GroupIdx);

		SortedGroupNames.Add(GroupName);
		TArray<int32>& GroupItems = GroupToItems.FindOrAdd(GroupName);

		// Hard ref: loaded with the variant, or genuinely unset.
		UPCGExAssetCollection* Src = Group.SourceCollection;
		if (!Src)
		{
			continue;
		}

		// A never-rebuilt legacy source has no EntryIds -- assign them now so tiles are bindable.
		PCGExCollectionEditorUtils::EnsureEntryIds(Src, /*bNotify=*/false);

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
			NewItem.Label = FText::FromString(Entry->EDITOR_GetDisplayName());

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
			RuleItem.bRulePayloadSet = Rule.Entry.IsValid();
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

	// Synthetic group: one tile per material swap row, in priority order. Rows that can never apply stay visible,
	// flagged with the Swap node's own reachability rule.
	if (!Variant->MaterialSwaps.IsEmpty())
	{
		SortedGroupNames.Add(PCGExVariantGrid::MaterialSwapsGroupName);
		TArray<int32>& SwapItems = GroupToItems.FindOrAdd(PCGExVariantGrid::MaterialSwapsGroupName);

		TArray<PCGExCollections::FMaterialSwapRow> ApplicableRows;
		TArray<bool> Unreachable;
		int32 NumUnreachable = 0;
		int32 NumAttributeClauses = 0;
		Variant->GetMaterialSwaps(ApplicableRows, NumUnreachable, NumAttributeClauses, &Unreachable);

		for (int32 r = 0; r < Variant->MaterialSwaps.Num(); r++)
		{
			const FPCGExMaterialSwap& Swap = Variant->MaterialSwaps[r];

			FPCGExVariantGridItem& SwapItem = Items.Emplace_GetRef();
			SwapItem.MaterialSwapIdx = r;
			SwapItem.SourceThumbPath = Swap.MatchMaterial.ToSoftObjectPath();
			SwapItem.OverrideThumbPath = Swap.Replacement.ToSoftObjectPath();
			SwapItem.bMaterialSwapGated = Swap.bUseTagFilter;
			SwapItem.bMaterialSwapUnreachable = Unreachable.IsValidIndex(r) && Unreachable[r];
			SwapItem.bMaterialSwapMatchUnset = Swap.MatchMaterial.IsNull();

			const FText MatchName = Swap.MatchMaterial.IsNull() ? LOCTEXT("UnsetMaterialName", "Unset") : FText::FromString(Swap.MatchMaterial.GetAssetName());
			const FText ReplacementName = Swap.Replacement.IsNull() ? INVTEXT("?") : FText::FromString(Swap.Replacement.GetAssetName());
			SwapItem.Label = FText::Format(LOCTEXT("MaterialSwapLabelFmt", "{0} -> {1}"), MatchName, ReplacementName);

			SwapItems.Add(Items.Num() - 1);
		}
	}
}

void SPCGExVariantGridView::ReconcileLayout(const TArray<FPCGExVariantGridItem>& OldItems, const TArray<FName>& OldGroupNames, const TMap<FName, TArray<int32>>& OldGroupToItems)
{
	TMap<FName, TSharedPtr<SPCGExCollectionCategoryGroup>> NewGroupWidgets;
	TMap<int32, TSharedPtr<SPCGExVariantGridTile>> ReusedTiles;
	TArray<FName> GroupsToPopulate;

	for (const FName& GroupName : SortedGroupNames)
	{
		if (NewGroupWidgets.Contains(GroupName))
		{
			// Same-named source assets merge in GroupToItems — one widget serves the merged group.
			continue;
		}

		const TSharedPtr<SPCGExCollectionCategoryGroup>* ExistingWidget = GroupWidgets.Find(GroupName);
		const TArray<int32>* NewIdx = GroupToItems.Find(GroupName);
		const TArray<int32>* OldIdx = OldGroupToItems.Find(GroupName);
		const int32 Count = NewIdx ? NewIdx->Num() : 0;

		bool bSameMembers = ExistingWidget && ExistingWidget->IsValid() && OldIdx && NewIdx && OldIdx->Num() == NewIdx->Num();
		if (bSameMembers)
		{
			for (int32 k = 0; k < Count; k++)
			{
				if (!PCGExVariantGrid::SameIdentity(OldItems[(*OldIdx)[k]], Items[(*NewIdx)[k]]))
				{
					bSameMembers = false;
					break;
				}
			}
		}

		if (bSameMembers)
		{
			// Widget and tiles survive as-is; global item indices may have shifted, so tiles
			// rebind their index and refresh display data in place.
			NewGroupWidgets.Add(GroupName, *ExistingWidget);
			for (int32 k = 0; k < Count; k++)
			{
				if (const TSharedPtr<SPCGExVariantGridTile>* Tile = ActiveTiles.Find((*OldIdx)[k]); Tile && Tile->IsValid())
				{
					(*Tile)->UpdateItem((*NewIdx)[k], Items[(*NewIdx)[k]]);
					ReusedTiles.Add((*NewIdx)[k], *Tile);
				}
			}
		}
		else
		{
			// Membership changed: keep the widget shell when one exists (header, expansion
			// state, scroll anchoring) and rebuild only its tiles below.
			TSharedPtr<SPCGExCollectionCategoryGroup> GroupWidget = ExistingWidget && ExistingWidget->IsValid() ? *ExistingWidget : nullptr;
			if (GroupWidget.IsValid())
			{
				GroupWidget->SetEntryCount(Count);
			}
			else
			{
				GroupWidget = MakeGroupWidget(GroupName, Count);
			}
			NewGroupWidgets.Add(GroupName, GroupWidget);
			GroupsToPopulate.Add(GroupName);
		}
	}

	// Re-slot the scroll box only when the ordered group list changed (group added/removed/
	// renamed) — re-adding the same widget instances keeps their children (tiles, thumbnails)
	// alive, so this is a relayout, not a rebuild.
	if (OldGroupNames != SortedGroupNames)
	{
		GroupScrollBox->ClearChildren();
		TSet<FName> Slotted;
		for (const FName& GroupName : SortedGroupNames)
		{
			if (Slotted.Contains(GroupName))
			{
				continue;
			}
			Slotted.Add(GroupName);
			GroupScrollBox->AddSlot()
			[
				NewGroupWidgets.FindChecked(GroupName).ToSharedRef()
			];
		}
	}

	GroupWidgets = MoveTemp(NewGroupWidgets);
	ActiveTiles = MoveTemp(ReusedTiles); // stale tiles of changed/removed groups drop here

	for (const FName& GroupName : GroupsToPopulate)
	{
		if (CollapsedGroups.Contains(GroupName))
		{
			// Collapsed: drop stale tile children now; expansion repopulates from live Items.
			if (const TSharedPtr<SPCGExCollectionCategoryGroup>* GroupWidget = GroupWidgets.Find(GroupName); GroupWidget && GroupWidget->IsValid())
			{
				(*GroupWidget)->ClearTiles();
			}
		}
		else
		{
			PopulateGroupTiles(GroupName);
		}
	}

	ApplySelectionVisuals();
}

TSharedRef<SPCGExCollectionCategoryGroup> SPCGExVariantGridView::MakeGroupWidget(const FName GroupName, const int32 EntryCount)
{
	return SNew(SPCGExCollectionCategoryGroup)
		.CategoryName(GroupName)
		.EntryCount(EntryCount)
		.bIsCollapsed(CollapsedGroups.Contains(GroupName))
		.bAllowRename(false) // group names mirror source asset names — never editable here
		// No OnAddToCategory, so groups show no [+]: sources and swaps are added from the toolbar.
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
			.OnRevokeSwap_Raw(this, &SPCGExVariantGridView::RevokeSwap)
			.OnDeleteRule_Raw(this, &SPCGExVariantGridView::DeleteRule);

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

	// Unset material swap rows: the "+" picks the replacement material.
	if (State == EPCGExVariantTileState::MaterialSwapUnset)
	{
		const int32 RowIndex = Item.MaterialSwapIdx;
		const FSoftObjectPath ExpectedMatch = Item.SourceThumbPath;

		FAssetPickerConfig PickerConfig;
		PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSPLambda(this, [this, RowIndex, ExpectedMatch](const FAssetData& InMaterial)
		{
			FSlateApplication::Get().DismissAllMenus();
			SetMaterialSwapReplacement(RowIndex, ExpectedMatch, InMaterial);
		});

		FSlateApplication::Get().PushMenu(
			AsShared(),
			FWidgetPath(),
			PCGExCollectionEditorUtils::MakeAssetPickerMenu(MoveTemp(PickerConfig), UMaterialInterface::StaticClass()),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return;
	}

	// Unset rule tiles get the same chooser, targeting the rule's payload; the copy option
	// only shows when a loaded source actually stages the matched asset (the seed entry).
	if (State == EPCGExVariantTileState::RuleUnset)
	{
		if (!Variant->PathOverrides.IsValidIndex(Item.PathRuleIdx))
		{
			return;
		}

		const UScriptStruct* SeedStruct = nullptr;
		const UPCGExAssetCollection* SeedHost = nullptr;
		const FPCGExAssetCollectionEntry* Seed = PCGExCollectionEditorUtils::FindRuleSeedEntry(
			Variant, Variant->PathOverrides[Item.PathRuleIdx].MatchAsset, SeedStruct, SeedHost);

		FMenuBuilder RuleMenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		if (Seed)
		{
			RuleMenuBuilder.BeginSection(NAME_None, LOCTEXT("DeclareSwapCopySection", "Declare Swap"));
			RuleMenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("DeclareSwapCopy", "Copy Source Entry ({0})"), PCGExCollectionEditorUtils::GetEntryTypeLabel(SeedStruct)),
				LOCTEXT("DeclareRuleCopyTooltip", "The payload starts as a full copy of the first source entry staging this asset -- swap-the-asset becomes a one-field edit; weights, variations and tags carry over."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(this, [this, ItemIndex]()
				{
					DeclareRuleSwapAs(ItemIndex, nullptr);
				})));
			RuleMenuBuilder.EndSection();
		}

		RuleMenuBuilder.BeginSection(NAME_None, LOCTEXT("DeclareSwapTypeSection", "Swap With Type"));
		TArray<const UScriptStruct*> RuleEntryTypes;
		PCGExCollectionEditorUtils::GetAllConcreteEntryTypes(RuleEntryTypes);
		for (const UScriptStruct* EntryStruct : RuleEntryTypes)
		{
			const FText Label = PCGExCollectionEditorUtils::GetEntryTypeLabel(EntryStruct);
			RuleMenuBuilder.AddMenuEntry(
				Label,
				FText::Format(LOCTEXT("DeclareRuleTypeTooltip", "The payload starts as a fresh {0} entry."), Label),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(this, [this, ItemIndex, EntryStruct]()
				{
					DeclareRuleSwapAs(ItemIndex, EntryStruct);
				})));
		}
		RuleMenuBuilder.EndSection();

		FSlateApplication::Get().PushMenu(
			AsShared(),
			FWidgetPath(),
			RuleMenuBuilder.MakeWidget(),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return;
	}

	// Declare on pass-through, or specialize a rule-covered entry (explicit rows take
	// precedence over rules at bake time, so this cleanly overrides the rule per-entry).
	if ((State != EPCGExVariantTileState::PassThrough && State != EPCGExVariantTileState::SwappedByRule)
		|| !Variant->Sources.IsValidIndex(Item.GroupIdx))
	{
		return;
	}

	UPCGExAssetCollection* Src = Variant->Sources[Item.GroupIdx].SourceCollection;
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
	UPCGExAssetCollection* Src = Group.SourceCollection;
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

	EditVariant(LOCTEXT("DeclareSwap", "Declare Entry Swap"), [&](UPCGExVariantCollection& InVariant)
	{
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

		// Cross-ASSET copy: the payload's Instanced subobjects and per-entry export state (e.g. a
		// PCGData entry's ExportedDataAsset and slot collections) are still the source's -- detach
		// them, per the standing rule for every entry copy. A fresh payload only carried base fields.
		PCGExCollectionHelpers::DetachCopiedEntry(EntryStruct, NewRow.Entry.GetMutableMemory(), bCopySource ? SourceEntry.Entry : nullptr, &InVariant);

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
	});

	SelectedItem = ItemIndex;
	RefreshGrid();
}

void SPCGExVariantGridView::DeclareRuleSwapAs(const int32 ItemIndex, const UScriptStruct* EntryStruct)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	const FPCGExVariantGridItem& Item = Items[ItemIndex];
	if (Item.GetState() != EPCGExVariantTileState::RuleUnset || !Variant->PathOverrides.IsValidIndex(Item.PathRuleIdx))
	{
		return;
	}

	FPCGExVariantPathOverride& Rule = Variant->PathOverrides[Item.PathRuleIdx];

	// Re-resolve the seed — the menu may be stale (undo while open); a vanished seed degrades
	// the copy option to a no-op, same contract as DeclareSwapAs.
	const UScriptStruct* SeedStruct = nullptr;
	const UPCGExAssetCollection* SeedHost = nullptr;
	const FPCGExAssetCollectionEntry* Seed = PCGExCollectionEditorUtils::FindRuleSeedEntry(Variant, Rule.MatchAsset, SeedStruct, SeedHost);

	const bool bCopySeed = EntryStruct == nullptr;
	if (bCopySeed)
	{
		if (!Seed)
		{
			return;
		}
		EntryStruct = SeedStruct;
	}

	if (!EntryStruct)
	{
		return;
	}

	EditVariant(LOCTEXT("DeclareRuleSwap", "Declare Asset Swap"), [&](UPCGExVariantCollection& InVariant)
	{
		if (bCopySeed)
		{
			// Full copy — swap-the-asset becomes a one-field edit and everything carries over.
			Rule.Entry.InitializeAs(EntryStruct, reinterpret_cast<const uint8*>(Seed));
		}
		else
		{
			// Fresh payload of the chosen type; when a seed exists, carry its BASE fields over
			// (weight/category/tags/variations) so the swap slots into the same pick distribution.
			Rule.Entry.InitializeAs(EntryStruct);
			if (Seed)
			{
				FPCGExAssetCollectionEntry::StaticStruct()->CopyScriptStruct(Rule.Entry.GetMutableMemory(), Seed, 1);
			}
		}

		// Cross-ASSET copy: detach the Instanced subobjects and per-entry export state the payload
		// still shares with its seed, per the standing rule for every entry copy.
		PCGExCollectionHelpers::DetachCopiedEntry(EntryStruct, Rule.Entry.GetMutableMemory(), bCopySeed ? Seed : nullptr, &InVariant);

		if (FPCGExAssetCollectionEntry* Payload = Rule.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>())
		{
			Payload->EntryId = 0;
			// Bake the seed collection's Global channels into the payload — the variant host
			// cannot provide typed globals (ISM/skinned descriptors).
			if (SeedHost)
			{
				Payload->ResolveGlobalsToLocal(SeedHost);
			}
		}
	});

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

	if (Item.MaterialSwapIdx != INDEX_NONE)
	{
		const int32 RowIndex = Item.MaterialSwapIdx;
		if (!Variant->MaterialSwaps.IsValidIndex(RowIndex) || Variant->MaterialSwaps[RowIndex].Replacement.IsNull())
		{
			return;
		}

		// Replacement only -- the row and its matched material survive as an unset row (DeleteRule removes the row).
		EditVariant(LOCTEXT("ClearMaterialReplacement", "Clear Material Swap Replacement"), [RowIndex](UPCGExVariantCollection& InVariant)
		{
			InVariant.MaterialSwaps[RowIndex].Replacement.Reset();
		});

		RefreshGrid();
		return;
	}

	if (Item.bIsRuleDefinition)
	{
		if (!Variant->PathOverrides.IsValidIndex(Item.PathRuleIdx) || !Variant->PathOverrides[Item.PathRuleIdx].Entry.IsValid())
		{
			return;
		}

		// Payload only — the rule and its asset reference survive as an unset rule
		// (DeleteRule is the whole-rule removal).
		EditVariant(LOCTEXT("RevokeRuleSwap", "Remove Asset Swap Payload"), [RuleIdx = Item.PathRuleIdx](UPCGExVariantCollection& InVariant)
		{
			InVariant.PathOverrides[RuleIdx].Entry.Reset();
		});

		RefreshGrid();
		return;
	}

	if (Item.OverrideRowIdx == INDEX_NONE || !Variant->Sources.IsValidIndex(Item.GroupIdx))
	{
		return;
	}

	EditVariant(LOCTEXT("RevokeSwap", "Remove Entry Swap"), [GroupIdx = Item.GroupIdx, RowIdx = Item.OverrideRowIdx](UPCGExVariantCollection& InVariant)
	{
		InVariant.Sources[GroupIdx].Overrides.RemoveAt(RowIdx);
	});

	RefreshGrid();
}

void SPCGExVariantGridView::DeleteRule(const int32 ItemIndex)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	const FPCGExVariantGridItem& Item = Items[ItemIndex];

	if (Item.MaterialSwapIdx != INDEX_NONE)
	{
		const int32 RowIndex = Item.MaterialSwapIdx;
		if (!Variant->MaterialSwaps.IsValidIndex(RowIndex))
		{
			return;
		}

		const int32 SelectedRow = Items.IsValidIndex(SelectedItem) ? Items[SelectedItem].MaterialSwapIdx : INDEX_NONE;

		EditVariant(LOCTEXT("DeleteMaterialSwap", "Delete Material Swap"), [RowIndex](UPCGExVariantCollection& InVariant)
		{
			InVariant.MaterialSwaps.RemoveAt(RowIndex);
		});

		if (SelectedRow == INDEX_NONE)
		{
			RefreshGrid();
			return;
		}

		// The selection follows its row: the rows after the deleted one move up, the deleted row's selection clears.
		RefreshGrid(/*bRefreshDetailPanel=*/ false);
		SelectMaterialSwap(SelectedRow == RowIndex ? INDEX_NONE : (SelectedRow > RowIndex ? SelectedRow - 1 : SelectedRow));
		return;
	}

	if (!Item.bIsRuleDefinition || !Variant->PathOverrides.IsValidIndex(Item.PathRuleIdx))
	{
		return;
	}

	EditVariant(LOCTEXT("DeleteRule", "Delete Asset Swap Rule"), [RuleIdx = Item.PathRuleIdx](UPCGExVariantCollection& InVariant)
	{
		InVariant.PathOverrides.RemoveAt(RuleIdx);
	});

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

void SPCGExVariantGridView::RefreshDetailPanel()
{
	// Re-focus the grid first so a focused field commits through OnFocusLost before its widget is torn down.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Cleared);
	}

	UpdateDetailForSelection();

	if (StructDetailView.IsValid())
	{
		if (IDetailsView* Inner = StructDetailView->GetDetailsView())
		{
			Inner->ForceRefresh();
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
				if (UPCGExAssetCollection* Src = Variant->Sources[Item.GroupIdx].SourceCollection)
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

		// The copy has no outer; the package lets customizations reach the host (IPropertyHandle::GetOuterPackages).
		if (const UPCGExVariantCollection* Host = Collection.Get())
		{
			CurrentStructScope->SetPackage(Host->GetPackage());
		}

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

	const FText TransactionText = Items[SelectedItem].MaterialSwapIdx != INDEX_NONE
		                              ? LOCTEXT("EditMaterialSwap", "Edit Material Swap")
		                              : LOCTEXT("EditSwapEntry", "Edit Swap Entry");
	const uint8* PanelMemory = CurrentStructScope->GetStructMemory();

	EditVariant(TransactionText, [PayloadStruct, PayloadMemory, PanelMemory](UPCGExVariantCollection&)
	{
		// Panel-owned members only: the rest may have been replaced by a restage since the panel copy was taken
		// (an export's instanced subobjects), and copying it back reinstates what that restage retired.
		for (TFieldIterator<FProperty> It(PayloadStruct); It; ++It)
		{
			if (PCGExCollectionEditorUtils::IsPanelOwnedMember(*It))
			{
				const int32 Offset = It->GetOffset_ForInternal();
				It->CopyCompleteValue(PayloadMemory + Offset, PanelMemory + Offset);
			}
		}
	});

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

	// Material swap rows: the row struct itself is the payload (no FInstancedStruct).
	if (InItem.MaterialSwapIdx != INDEX_NONE)
	{
		if (!Variant->MaterialSwaps.IsValidIndex(InItem.MaterialSwapIdx))
		{
			return false;
		}

		OutStruct = FPCGExMaterialSwap::StaticStruct();
		OutMemory = reinterpret_cast<uint8*>(&Variant->MaterialSwaps[InItem.MaterialSwapIdx]);
		return true;
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

void SPCGExVariantGridView::AddMaterialSwap(const FAssetData& InMaterial)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !InMaterial.IsValid())
	{
		return;
	}

	EditVariant(LOCTEXT("AddMaterialSwap", "Add Material Swap"), [&InMaterial](UPCGExVariantCollection& InVariant)
	{
		InVariant.MaterialSwaps.AddDefaulted_GetRef().MatchMaterial = TSoftObjectPtr<UMaterialInterface>(InMaterial.GetSoftObjectPath());
	});

	RefreshGrid(/*bRefreshDetailPanel=*/ false);
	SelectMaterialSwap(Variant->MaterialSwaps.Num() - 1);
}

void SPCGExVariantGridView::SetMaterialSwapReplacement(const int32 RowIndex, const FSoftObjectPath& InExpectedMatch, const FAssetData& InMaterial)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant || !InMaterial.IsValid() || !Variant->MaterialSwaps.IsValidIndex(RowIndex))
	{
		return;
	}

	const FPCGExMaterialSwap& Row = Variant->MaterialSwaps[RowIndex];
	if (!Row.Replacement.IsNull() || Row.MatchMaterial.ToSoftObjectPath() != InExpectedMatch)
	{
		return;
	}

	EditVariant(LOCTEXT("SetMaterialReplacement", "Set Material Swap Replacement"), [RowIndex, &InMaterial](UPCGExVariantCollection& InVariant)
	{
		InVariant.MaterialSwaps[RowIndex].Replacement = TSoftObjectPtr<UMaterialInterface>(InMaterial.GetSoftObjectPath());
	});

	RefreshGrid(/*bRefreshDetailPanel=*/ false);
	SelectMaterialSwap(RowIndex);
}

void SPCGExVariantGridView::SelectMaterialSwap(const int32 RowIndex)
{
	SelectedItem = FindMaterialSwapItem(RowIndex);
	ApplySelectionVisuals();
	UpdateDetailForSelection();

	// No tile while its group is collapsed; the details pane still shows the row.
	if (const TSharedPtr<SPCGExVariantGridTile>* Tile = ActiveTiles.Find(SelectedItem); Tile && Tile->IsValid())
	{
		GroupScrollBox->ScrollDescendantIntoView(*Tile, /*InAnimateScroll=*/ true, EDescendantScrollDestination::IntoView);
	}
}

int32 SPCGExVariantGridView::FindMaterialSwapItem(const int32 RowIndex) const
{
	if (RowIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	return Items.IndexOfByPredicate([RowIndex](const FPCGExVariantGridItem& InItem) { return InItem.MaterialSwapIdx == RowIndex; });
}

void SPCGExVariantGridView::EditVariant(const FText& InTransactionText, const TFunctionRef<void(UPCGExVariantCollection&)> InEdit)
{
	UPCGExVariantCollection* Variant = Collection.Get();
	if (!Variant)
	{
		return;
	}

	// Declared before the transaction so it outlives it: ending the transaction raises OnObjectTransacted.
	TGuardValue<bool> SyncGuard(bIsSyncing, true);
	FScopedTransaction Transaction(InTransactionText);
	Variant->Modify();
	InEdit(*Variant);
	Variant->PostEditChange();
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
		EditVariant(LOCTEXT("AddSources", "Add Variant Sources"), [&ToAdd](UPCGExVariantCollection& InVariant)
		{
			for (UPCGExAssetCollection* Source : ToAdd)
			{
				InVariant.Sources.AddDefaulted_GetRef().SourceCollection = Source;
			}
		});

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
