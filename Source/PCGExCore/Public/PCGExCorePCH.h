// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Shared PCH - use only on modules with 25+ cpp files
//
// If you get PCH memory exhaustion errors (C3859/C1076), add to PCGExCore.Build.cs:
//   PublicDefinitions.Add("PCGEX_FAT_PCH=0");

#pragma once

// Core UE
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

// FText lacks native GetTypeHash and operator== — required by engine's DefaultOperationTraits<FText>::Equal()
// and TSet<FText> instantiation. Must be defined BEFORE PCGMetadataAttributeTraits.h is included.
#ifndef PCGEX_FTEXT_SHIMS_DEFINED
#define PCGEX_FTEXT_SHIMS_DEFINED
FORCEINLINE uint32 GetTypeHash(const FText& Value) { return GetTypeHash(Value.ToString()); }
FORCEINLINE bool operator==(const FText& A, const FText& B) { return A.EqualTo(B); }
#endif

// PCG core - precompiled here so modules don't re-process them
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "PCGCommon.h"

// Foundational
#include "PCGExCommon.h"
#include "Core/PCGExContext.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExDataTags.h"

// Heavy hitter (289 includes across codebase)
#include "Math/PCGExMath.h"
#include "Utils/PCGExCompare.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Details/PCGExSettingsDetails.h"
