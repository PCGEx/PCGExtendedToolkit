// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * Built-in compact inline widget factories for FPCGExProperty_Vector / _Vector2 / _Rotator.
 *
 * Registered by the module at startup; provides single-row N-component editors that replace
 * the UE default expandable struct widgets for these property types.
 */
namespace PCGExBuiltInInlineWidgets
{
	void RegisterAll();
}
