// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsSettings.h"

#include "CoreMinimal.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExCoreSettingsCache.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExBoundsEvaluator.h"

void UPCGExCollectionsSettings::PostLoad()
{
	Super::PostLoad();
	UpdateSettingsCaches();
}

#if WITH_EDITOR
void UPCGExCollectionsSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateSettingsCaches();
}
#endif

void UPCGExCollectionsSettings::UpdateSettingsCaches() const
{
#define PCGEX_PUSH_SETTING(_MODULE, _SETTING) PCGEX_SETTINGS_INST(_MODULE)._SETTING = _SETTING;

	PCGEX_PUSH_SETTING(Collections, bDisableCollisionByDefault)

#undef PCGEX_PUSH_SETTING

	// Resolve soft class paths to UClass* in the cache (runs on game thread in PostLoad/PostEditChangeProperty)
	auto& Cache = PCGEX_SETTINGS_INST(Collections);
	Cache.DefaultLevelExporterClass = DefaultLevelExporterClass.TryLoadClass<UPCGExLevelDataExporter>();
	Cache.DefaultContentFilterClass = DefaultContentFilterClass.TryLoadClass<UPCGExActorContentFilter>();
	Cache.DefaultBoundsEvaluatorClass = DefaultBoundsEvaluatorClass.TryLoadClass<UPCGExBoundsEvaluator>();
}
