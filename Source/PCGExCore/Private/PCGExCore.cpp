// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCore.h"

#include "Data/PCGExSubAccessor.h"
#include "Utils/PCGExCurveLookup.h"

#if WITH_EDITOR
#include "AssetTypeActions_Base.h"
#include "Data/Bitmasks/PCGExBitmaskCollection.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "PCGExCoreEditor/Public/PCGExAssetTypesMacros.h"
#include "Sorting/PCGExSortingRuleProvider.h"
#include "UObject/UObjectGlobals.h"
#endif

PCGEX_IMPLEMENT_MODULE(FPCGExCoreModule, PCGExCore)

void FPCGExCoreModule::StartupModule()
{
	IPCGExLegacyModuleInterface::StartupModule();
	PCGExData::FSubAccessorRegistry::Initialize();

	// Loads the shipped default curves on the game thread, before any worker can ask for one.
	PCGExCurves::GetDefaultCurve(PCGExCurves::WeightDistributionLinear);
#if WITH_EDITOR
	PackageReloadedHandle = FCoreUObjectDelegates::OnPackageReloaded.AddStatic(&PCGExCurves::EDITOR_RepointDefaultCurves);
#endif
}

void FPCGExCoreModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnPackageReloaded.Remove(PackageReloadedHandle);
#endif
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#if WITH_EDITOR
void FPCGExCoreModule::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
	IPCGExModuleInterface::RegisterToEditor(InStyle);

	PCGEX_START_PCG_REGISTRATION
	PCGEX_REGISTER_DATA_TYPE(SortRule, SortRule)

	PCGEX_REGISTER_PIN_ICON(OUT_Special)
	PCGEX_REGISTER_PIN_ICON(IN_Special)

	PCGEX_REGISTER_PIN_ICON(OUT_RecursionTracker)
	PCGEX_REGISTER_PIN_ICON(IN_RecursionTracker)

	// Bitmask collection: UAssetDefinition + UFactory in PCGExCoreEditor handle menu registration
}
#endif
