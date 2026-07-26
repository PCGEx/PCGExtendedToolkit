// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

namespace PCGExSpatial::NarrowPhase
{
	/**
	 * Pair-test registration entry points, one per shape .cpp. Helpers live in a
	 * file-named namespace, not an anonymous one -- unity builds merge TUs.
	 *
	 * New shape kind: declare here, implement alongside its pair tests, call from
	 * RegisterBuiltInPairTests(). StartupModule and the tests share that list.
	 */
	void RegisterOBBPairTests();
	void RegisterPolygonPairTests();
	void RegisterVolumePrimitivePairTests();
}
