// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollections.h"

#include "PCGExCollectionsSettings.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExComponentFixups.h"
#include "Helpers/PCGExLevelExportBuiltinHandlers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Styling/AppStyle.h"

#if PCGEX_ENGINE_VERSION > 506
#include "Data/Registry/PCGDataTypeRegistry.h" // PCGEX_PCG_DATA_REGISTRY
#endif

#include "Selectors/PCGExSelectorFactoryProvider.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGExCollectionsModule"

namespace PCGExCollectionsModule
{
	FDelegateHandle PostEngineInitHandle;
#if WITH_EDITOR
	FDelegateHandle BlueprintCompiledHandle;

	void OnBlueprintCompiled()
	{
		GetMutableDefault<UPCGExCollectionsSettings>()->UpdateSettingsCaches(/*bLoadClasses=*/false);
	}
#endif

	// A config-loaded class default gets no PostLoad, so the settings cache is pushed here; after engine init, when
	// content (Blueprint) classes resolve. A recompile supersedes a Blueprint default class in place: re-resolve then.
	void OnEngineInitialized()
	{
		GetMutableDefault<UPCGExCollectionsSettings>()->UpdateSettingsCaches();
#if WITH_EDITOR
		if (GEditor && !BlueprintCompiledHandle.IsValid())
		{
			BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddStatic(&OnBlueprintCompiled);
		}
#endif
	}
}

void FPCGExCollectionsModule::StartupModule()
{
	if (GEngine && GEngine->IsInitialized())
	{
		PCGExCollectionsModule::OnEngineInitialized();
	}
	else
	{
		PCGExCollectionsModule::PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddStatic(&PCGExCollectionsModule::OnEngineInitialized);
	}

	// We need this because the registry holds a reference to the collection ::StaticClass
	// and it cannot be access during initialization so we defer it here.
	PCGExAssetCollection::FTypeRegistry::ProcessPendingRegistrations();
	IPCGExLegacyModuleInterface::StartupModule();
	PCGExComponentFixups::RegisterBuiltins();
#if WITH_EDITOR
	// Level export handlers read their class default objects at registration, so this cannot ride a
	// static initializer either. Out-of-module handlers register from their own StartupModule.
	PCGExLevelExport::RegisterBuiltinHandlers();
#endif
}

void FPCGExCollectionsModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().Remove(PCGExCollectionsModule::PostEngineInitHandle);
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().Remove(PCGExCollectionsModule::BlueprintCompiledHandle);
	}
#endif
	PCGExCollections::CancelPendingLevelReleases();

	// Release built-in fixup handles before the delta registry goes out of scope.
	PCGExComponentFixups::UnregisterBuiltins();
#if WITH_EDITOR
	PCGExLevelExport::UnregisterBuiltinHandlers();
#endif
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#if WITH_EDITOR
void FPCGExCollectionsModule::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
	IPCGExLegacyModuleInterface::RegisterToEditor(InStyle);

	// FSlateIconFinder walks ClassIcon.<ClassName> across every registered style set; the stock assembly
	// root shares the PCGDataAsset "Data Asset" source glyph since that is what it exports as.
	InStyle->Set("ClassIcon.PCGExAssemblyRootActor", new FSlateVectorImageBrush(InStyle->RootToContentDir(TEXT("PCGEx_Editor_PCGDA_DataAsset"), TEXT(".svg")), FVector2D(16.0f)));
	InStyle->Set("ClassThumbnail.PCGExAssemblyRootActor", new FSlateVectorImageBrush(InStyle->RootToContentDir(TEXT("PCGEx_Editor_PCGDA_DataAsset"), TEXT(".svg")), FVector2D(64.0f)));

	PCGEX_REGISTER_PIN_ICON(IN_Selector)
	PCGEX_REGISTER_PIN_ICON(OUT_Selector)

	PCGEX_START_PCG_REGISTRATION
	PCGEX_REGISTER_DATA_TYPE(Selector, Selector)
}
#endif

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExCollectionsModule, PCGExCollections)
