// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGExSettingsCacheBody.h"

#define PCGEX_COLLECTIONS_SETTINGS PCGEX_SETTINGS_INST(Collections)

struct PCGEXCOLLECTIONS_API FPCGExCollectionsSettingsCache
{
	PCGEX_SETTING_CACHE_BODY(Collections)
	bool bDisableCollisionByDefault = true;

	/** Resolved default classes (populated from FSoftClassPath during push). */
	TSubclassOf<UObject> DefaultLevelExporterClass;
	TSubclassOf<UObject> DefaultContentFilterClass;
	TSubclassOf<UObject> DefaultBoundsEvaluatorClass;
};
