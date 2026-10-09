// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsSettings.h"

#include "CoreMinimal.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExCoreSettingsCache.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExActorMeshClassificator.h"
#include "Helpers/PCGExBoundsEvaluator.h"
#include "Helpers/PCGExLevelDataExporter.h"

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

void UPCGExCollectionsSettings::UpdateSettingsCaches(const bool bLoadClasses)
{
#define PCGEX_PUSH_SETTING(_MODULE, _SETTING) PCGEX_SETTINGS_INST(_MODULE)._SETTING = _SETTING;

	PCGEX_PUSH_SETTING(Collections, bDisableCollisionByDefault)

#undef PCGEX_PUSH_SETTING

	// Game thread only. A recompiled Blueprint is superseded in place (the old class is renamed REINST_), so the
	// compile hook re-resolves without loading: the new class is already in memory under the configured path.
	auto Resolve = [bLoadClasses](const FSoftClassPath& Path, const UClass* Base) -> UClass*
	{
		UClass* Class = bLoadClasses ? Path.TryLoadClass<UObject>() : Path.ResolveClass();
		return Class && Class->IsChildOf(Base) ? Class : nullptr;
	};

	auto& Cache = PCGEX_SETTINGS_INST(Collections);
	Cache.DefaultLevelExporterClass = Resolve(DefaultLevelExporterClass, UPCGExLevelDataExporter::StaticClass());
	Cache.DefaultContentFilterClass = Resolve(DefaultContentFilterClass, UPCGExActorContentFilter::StaticClass());
	Cache.DefaultBoundsEvaluatorClass = Resolve(DefaultBoundsEvaluatorClass, UPCGExBoundsEvaluator::StaticClass());
	Cache.DefaultMeshClassificatorClass = Resolve(DefaultMeshClassificatorClass, UPCGExActorMeshClassificator::StaticClass());

	ResolvedDefaultClasses.Reset(4);
	ResolvedDefaultClasses.Add(Cache.DefaultLevelExporterClass.Get());
	ResolvedDefaultClasses.Add(Cache.DefaultContentFilterClass.Get());
	ResolvedDefaultClasses.Add(Cache.DefaultBoundsEvaluatorClass.Get());
	ResolvedDefaultClasses.Add(Cache.DefaultMeshClassificatorClass.Get());

	Cache.SystemActorClasses = UPCGExActorContentFilter::KnownSystemActorClasses;
	Cache.SystemActorClasses.Append(AdditionalSystemActorClasses);
}
