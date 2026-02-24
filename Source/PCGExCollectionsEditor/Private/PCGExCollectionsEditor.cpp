// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditor.h"

#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "PCGExAssetTypesMacros.h"
#include "PCGExCollectionsEditorMenuUtils.h"
#include "PropertyEditorModule.h"
#include "Details/Collections/PCGExActorCollectionActions.h"
#include "Details/Collections/PCGExAssetEntryCustomization.h"
#include "Details/Collections/PCGExAssetGrammarCustomization.h"
#include "Details/Collections/PCGExFittingVariationsCustomization.h"
#include "Details/Collections/PCGExMaterialPicksCustomization.h"
#include "Details/Collections/PCGExMeshCollectionActions.h"
#include "Details/Collections/PCGExLevelCollectionActions.h"
#include "Details/Collections/PCGExPCGDataAssetCollectionActions.h"

#define LOCTEXT_NAMESPACE "FPCGExCollectionsEditorModule"

#undef LOCTEXT_NAMESPACE

void FPCGExCollectionsEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	PCGEX_REGISTER_CUSTO_START

	PCGEX_REGISTER_CUSTO("PCGExFittingVariations", FPCGExFittingVariationsCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideEntry", FPCGExMaterialOverrideEntryCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideSingleEntry", FPCGExMaterialOverrideSingleEntryCustomization)
	PCGEX_REGISTER_CUSTO("PCGExMaterialOverrideCollection", FPCGExMaterialOverrideCollectionCustomization)
	PCGEX_REGISTER_CUSTO("PCGExAssetGrammarDetails", FPCGExAssetGrammarCustomization)

#define PCGEX_REGISTER_ENTRY_CUSTOMIZATION(_CLASS, _NAME)\
	FAssetToolsModule::GetModule().Get().RegisterAssetTypeActions(MakeShared<FPCGEx##_CLASS##CollectionActions>());\
	PCGEX_REGISTER_CUSTO("PCGEx"#_CLASS"CollectionEntry", FPCGEx##_CLASS##EntryCustomization)

	PCGEX_FOREACH_ENTRY_TYPE_ALL(PCGEX_REGISTER_ENTRY_CUSTOMIZATION)

#undef PCGEX_REGISTER_ENTRY_CUSTOMIZATION
}

void FPCGExCollectionsEditorModule::RegisterMenuExtensions()
{
	IPCGExEditorModuleInterface::RegisterMenuExtensions();

	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* WorldAssetMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AssetActionsSubMenu"))
	{
		// Use a dynamic section here because we might have plugins registering at a later time
		FToolMenuSection& Section = WorldAssetMenu->AddDynamicSection(
			"PCGEx", FNewToolMenuDelegate::CreateLambda(
				[this](UToolMenu* ToolMenu)
				{
					if (!GEditor || GEditor->GetPIEWorldContext() || !ToolMenu) { return; }
					if (UContentBrowserAssetContextMenuContext* AssetMenuContext = ToolMenu->Context.FindContext<UContentBrowserAssetContextMenuContext>())
					{
						PCGExCollectionsEditorMenuUtils::CreateOrUpdatePCGExAssetCollectionsFromMenu(ToolMenu, AssetMenuContext->SelectedAssets);
					}
				}), FToolMenuInsert(NAME_None, EToolMenuInsertType::Default));
	}
}

PCGEX_IMPLEMENT_MODULE(FPCGExCollectionsEditorModule, PCGExCollectionsEditor)
