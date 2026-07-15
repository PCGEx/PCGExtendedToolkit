// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"

#ifndef PCGEX_VERSION
#define PCGEX_VERSION

#define PCGEX_ENGINE_VERSION ENGINE_MAJOR_VERSION * 100 + ENGINE_MINOR_VERSION

#define PCGEX_VERSION_NUM(_MAJOR, _MINOR, _PATCH) ((_MAJOR * 1000000) + (_MINOR * 10000) + (_PATCH * 100))
#define PCGEX_VERSION_NAME(_MAJOR, _MINOR, _PATCH) v_##_MAJOR##_##_MINOR##_##_PATCH
#define PCGEX_VERSION_BOOL(_MAJOR, _MINOR, _PATCH) b_##_MAJOR##_##_MINOR##_##_PATCH
#define PCGEX_VERSION_DECL(_MAJOR, _MINOR, _PATCH) int64 PCGEX_VERSION_NAME(_MAJOR, _MINOR, _PATCH) = PCGEX_VERSION_NUM(_MAJOR, _MINOR, _PATCH);
#define PCGEX_VERSION_DECL_LATEST(_MAJOR, _MINOR, _PATCH) constexpr int64 Latest = PCGEX_VERSION_NUM(_MAJOR, _MINOR, _PATCH);

#define PCGEX_IF_VERSION_LOWER(_MAJOR, _MINOR, _PATCH) \
constexpr PCGEX_VERSION_DECL(_MAJOR, _MINOR, _PATCH) \
if (PCGExDataVersion < PCGEX_VERSION_NAME(_MAJOR, _MINOR, _PATCH))

#define PCGEX_UPDATE_DATA_VERSION_TO_LATEST PCGExDataVersion = PCGExVersion::Latest;

namespace PCGExVersion
{
	// Purposefully not in sync with .uplugin
	// I was having too many issues trying keeping those in sync with iterative deprecation code
	// that required bumping the internal version more often than the user-facing one
	PCGEX_VERSION_DECL_LATEST(1, 76, 10)
}

#endif
