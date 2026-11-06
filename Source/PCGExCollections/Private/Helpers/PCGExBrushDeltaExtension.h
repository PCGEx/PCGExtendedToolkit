// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/** Brush shapes in actor deltas: volumes and brush shapes respawn with their polys, BSP, collision and navigation.
 *  Editor only: the rebuild runs through BSPUtils, and the polys are editor-only data. */
namespace PCGExBrushDeltaExtension
{
	/** Called from FPCGExCollectionsModule::StartupModule. */
	void Register();

	/** Called from FPCGExCollectionsModule::ShutdownModule. */
	void Unregister();
}

#endif
