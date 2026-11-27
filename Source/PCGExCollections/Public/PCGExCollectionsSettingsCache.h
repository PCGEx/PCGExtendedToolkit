// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSettingsCacheBody.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"

#define PCGEX_COLLECTIONS_SETTINGS PCGEX_SETTINGS_INST(Collections)

struct PCGEXCOLLECTIONS_API FPCGExCollectionsSettingsCache
{
	PCGEX_SETTING_CACHE_BODY(Collections)
	bool bDisableCollisionByDefault = true;

	/** Resolved default classes (populated from FSoftClassPath during push). */
	TSubclassOf<UObject> DefaultLevelExporterClass;
	TSubclassOf<UObject> DefaultContentFilterClass;
	TSubclassOf<UObject> DefaultBoundsEvaluatorClass;
	TSubclassOf<UObject> DefaultMeshClassificatorClass;
	TSubclassOf<UObject> DefaultSplineClassificatorClass;

	/** Combined set of actor class names treated as system/infrastructure: hardcoded baseline
	 *  in UPCGExActorContentFilter::KnownSystemActorClasses, plus runtime registrations, plus
	 *  user-config UPCGExCollectionsSettings::AdditionalSystemActorClasses. Single O(1) lookup. */
	TSet<FName> SystemActorClasses;
};
