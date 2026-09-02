// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollections.h"

#include "Core/PCGExAssetCollectionTypes.h"
#include "Helpers/PCGExComponentFixups.h"
#include "Helpers/PCGExLevelExportBuiltinHandlers.h"

#if WITH_EDITOR
#include "Styling/AppStyle.h"

#if PCGEX_ENGINE_VERSION > 506
#include "Data/Registry/PCGDataTypeRegistry.h" // PCGEX_PCG_DATA_REGISTRY
#endif

#include "Selectors/PCGExSelectorFactoryProvider.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGExCollectionsModule"

void FPCGExCollectionsModule::StartupModule()
{
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

	PCGEX_REGISTER_PIN_ICON(IN_Selector)
	PCGEX_REGISTER_PIN_ICON(OUT_Selector)

	PCGEX_START_PCG_REGISTRATION
	PCGEX_REGISTER_DATA_TYPE(Selector, Selector)
}
#endif

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExCollectionsModule, PCGExCollections)
