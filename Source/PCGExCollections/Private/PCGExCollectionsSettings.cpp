// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsSettings.h"

#include "CoreMinimal.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExCoreSettingsCache.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExActorMeshClassificator.h"
#include "Helpers/PCGExActorSplineClassificator.h"
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

	// Game thread only. The editor hooks pass bLoadClasses false and pick up whatever class object now lives at the
	// configured path: a Blueprint recompile keeps it (only a REINST_ copy is made), a package reload replaces it.
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
	Cache.DefaultSplineClassificatorClass = Resolve(DefaultSplineClassificatorClass, UPCGExActorSplineClassificator::StaticClass());

	// Pins the cached classes: unreferenced Blueprint classes are collectable outside the editor. Not a UPROPERTY: cooked
	// builds put this CDO in the disregard-for-GC pool, whose references are never traced (and fail VerifyGCAssumptions).
	ResolvedDefaultClasses.Reset(5);
	ResolvedDefaultClasses.Emplace(Cache.DefaultLevelExporterClass.Get());
	ResolvedDefaultClasses.Emplace(Cache.DefaultContentFilterClass.Get());
	ResolvedDefaultClasses.Emplace(Cache.DefaultBoundsEvaluatorClass.Get());
	ResolvedDefaultClasses.Emplace(Cache.DefaultMeshClassificatorClass.Get());
	ResolvedDefaultClasses.Emplace(Cache.DefaultSplineClassificatorClass.Get());

	Cache.SystemActorClasses = UPCGExActorContentFilter::KnownSystemActorClasses;
	Cache.SystemActorClasses.Append(AdditionalSystemActorClasses);
}
