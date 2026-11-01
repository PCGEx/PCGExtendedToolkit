// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExVariantCollectionEditor.h"

#include "AssetThumbnail.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/PCGExCollectionCategoryGroups.h"
#include "Details/Collections/SPCGExVariantGridView.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExVariantCollectionEditor"

FPCGExVariantCollectionEditor::FPCGExVariantCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

void FPCGExVariantCollectionEditor::CreateTabs(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs)
{
	// No entries tab (there is no homogeneous Entries array) — a Collection Settings tab
	// (full asset details, incl. raw Sources as escape hatch) plus the variant grid.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NotifyHook = nullptr;
	DetailsArgs.bAllowMultipleTopLevelObjects = false;

	const TSharedPtr<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(EditedCollection.Get());

	PCGExAssetCollectionEditor::TabInfos& SettingsInfos = OutTabs.Emplace_GetRef(FName("Collection"), DetailsView, FName("Collection Settings"));
	SettingsInfos.Icon = TEXT("Settings");

	// Grid LAST so the reverse-iteration loop in InitEditor makes it the leftmost tab
	// (same ordering contract as the base editor).
	if (!ThumbnailPool.IsValid())
	{
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
	}

	SAssignNew(VariantGrid, SPCGExVariantGridView)
	.Collection(Cast<UPCGExVariantCollection>(EditedCollection.Get()))
	.ThumbnailPool(ThumbnailPool)
	.TileSize(192.f);

	PCGExAssetCollectionEditor::TabInfos& GridInfos = OutTabs.Emplace_GetRef(FName("Swaps"), VariantGrid, FName("Swaps"));
	GridInfos.Icon = TEXT("Entries");
	GridInfos.bIsDetailsView = false;

	FToolBarBuilder HeaderToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	HeaderToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetHeaderToolbar(HeaderToolbarBuilder);
	GridInfos.Header = HeaderToolbarBuilder.MakeWidget();

	FToolBarBuilder FooterToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	FooterToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetFooterToolbar(FooterToolbarBuilder);
	GridInfos.Footer = FooterToolbarBuilder.MakeWidget();
}

void FPCGExVariantCollectionEditor::GetFilterableEntryStructs(TArray<const UScriptStruct*>& OutStructs) const
{
	PCGExCollectionCategoryGroups::GetAllRegisteredEntryStructs(OutStructs);
}

void FPCGExVariantCollectionEditor::RefreshFilteredPanels()
{
	if (VariantGrid.IsValid())
	{
		VariantGrid->RefreshDetailPanel();
	}
}

void FPCGExVariantCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FPCGExVariantCollectionEditor::MakeAddSourceMenu),
		LOCTEXT("AddSource", "Add Source"),
		LOCTEXT("AddSourceTooltip", "Add a source collection to theme. Its entries appear as swap candidates in the grid."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"));

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FPCGExVariantCollectionEditor::MakeAddAssetSwapMenu),
		LOCTEXT("AddAssetSwap", "Add Asset Swap"),
		LOCTEXT("AddAssetSwapTooltip", "Add an asset-path swap rule: every source entry staging the picked asset swaps to the rule's payload (unless an explicit per-entry swap overrides it)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Link"));

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FPCGExVariantCollectionEditor::MakeAddMaterialSwapMenu),
		LOCTEXT("AddMaterialSwap", "Add Material Swap"),
		LOCTEXT("AddMaterialSwapTooltip", "Add a material swap: every spawned mesh slot rendering the picked material renders the row's replacement instead (staged mesh selector). A material's rows are tried in order."),
		FSlateIconFinder::FindIcon("ClassIcon.Material")); // No ClassIcon.MaterialInterface is registered.
}

TSharedRef<SWidget> FPCGExVariantCollectionEditor::MakeAddSourceMenu()
{
	FAssetPickerConfig PickerConfig;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FPCGExVariantCollectionEditor::OnSourceAssetPicked);
	PickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda(
		[WeakEdited = EditedCollection](const FAssetData& Asset)
		{
			// Other variants ARE legal sources: their payload entries carry stable EntryIds,
			// so theming a variant enables daisy-chained Swap nodes (X→A, then A→B). Only
			// self-reference is excluded. Mappings never recurse into a source's own mappings,
			// so even accidental A↔B setups are inert rather than cyclic.
			if (const UPCGExAssetCollection* Edited = WeakEdited.Get();
				Edited && Asset.GetSoftObjectPath() == FSoftObjectPath(Edited))
			{
				return true;
			}
			return false;
		});

	return PCGExCollectionEditorUtils::MakeAssetPickerMenu(MoveTemp(PickerConfig), UPCGExAssetCollection::StaticClass());
}

void FPCGExVariantCollectionEditor::OnSourceAssetPicked(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();

	// The grid owns the single add-source path (filtering, transaction, toasts, refresh).
	if (VariantGrid.IsValid())
	{
		VariantGrid->AddSourcesFromAssets({AssetData});
	}
}

TSharedRef<SWidget> FPCGExVariantCollectionEditor::MakeAddAssetSwapMenu()
{
	// No class filter: rules match any staged asset path (meshes, actors, levels, ...).
	FAssetPickerConfig PickerConfig;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FPCGExVariantCollectionEditor::OnSwapAssetPicked);

	return PCGExCollectionEditorUtils::MakeAssetPickerMenu(MoveTemp(PickerConfig));
}

TSharedRef<SWidget> FPCGExVariantCollectionEditor::MakeAddMaterialSwapMenu()
{
	FAssetPickerConfig PickerConfig;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FPCGExVariantCollectionEditor::OnMaterialSwapPicked);

	return PCGExCollectionEditorUtils::MakeAssetPickerMenu(MoveTemp(PickerConfig), UMaterialInterface::StaticClass());
}

void FPCGExVariantCollectionEditor::OnMaterialSwapPicked(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();

	// The grid owns the add path (transaction, refresh, selecting the new row for editing).
	if (VariantGrid.IsValid())
	{
		VariantGrid->AddMaterialSwap(AssetData);
	}
}

void FPCGExVariantCollectionEditor::OnSwapAssetPicked(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();

	UPCGExVariantCollection* Variant = Cast<UPCGExVariantCollection>(EditedCollection.Get());
	if (!Variant)
	{
		return;
	}

	const FSoftObjectPath MatchPath = AssetData.GetSoftObjectPath();

	for (const FPCGExVariantPathOverride& Rule : Variant->PathOverrides)
	{
		if (Rule.MatchAsset == MatchPath)
		{
			FNotificationInfo Info(LOCTEXT("RuleAlreadyExists", "An asset swap rule for this asset already exists."));
			Info.ExpireDuration = 3.f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return;
		}
	}

	// Seed the payload from the first source entry staging this asset — gives the rule a
	// concrete type and sensible defaults. Falls back to an unset payload when nothing
	// matches yet: the tile then renders as an unswapped rule whose "+" declares the payload.
	const UScriptStruct* SeedStruct = nullptr;
	const UPCGExAssetCollection* SeedCollection = nullptr;
	const FPCGExAssetCollectionEntry* SeedEntry = PCGExCollectionEditorUtils::FindRuleSeedEntry(Variant, MatchPath, SeedStruct, SeedCollection);

	{
		FScopedTransaction Transaction(LOCTEXT("AddAssetSwapTransaction", "Add Asset Swap Rule"));
		Variant->Modify();

		FPCGExVariantPathOverride& NewRule = Variant->PathOverrides.AddDefaulted_GetRef();
		NewRule.MatchAsset = MatchPath;

		if (SeedStruct && SeedEntry)
		{
			NewRule.Entry.InitializeAs(SeedStruct, reinterpret_cast<const uint8*>(SeedEntry));

			// Cross-ASSET copy: detach the Instanced subobjects and per-entry export state the
			// payload still shares with the seed (e.g. a PCGData entry's ExportedDataAsset), per
			// the standing rule for every entry copy.
			PCGExCollectionHelpers::DetachCopiedEntry(SeedStruct, NewRule.Entry.GetMutableMemory(), SeedEntry, Variant);

			if (FPCGExAssetCollectionEntry* Payload = NewRule.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>())
			{
				Payload->EntryId = 0;
				// Bake the seed collection's Global channels into the payload — the variant
				// host cannot provide typed globals (ISM/skinned descriptors).
				Payload->ResolveGlobalsToLocal(SeedCollection);
			}
		}
		else
		{
			FNotificationInfo Info(LOCTEXT("RuleNoMatchYet", "No declared source currently stages this asset — the rule is inert until a payload is declared. Use the tile's + button to declare what it swaps to."));
			Info.ExpireDuration = 5.f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		Variant->PostEditChange();
	}

	if (VariantGrid.IsValid())
	{
		VariantGrid->RefreshGrid();
	}
}

#undef LOCTEXT_NAMESPACE
