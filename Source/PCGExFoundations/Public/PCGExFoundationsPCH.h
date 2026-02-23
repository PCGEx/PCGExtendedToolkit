// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Tier 2 shared PCH -- extends Core PCH with Filters, Blending, and Foundations headers.
// Consumed by all modules that depend on PCGExFoundations (~24 modules).

#pragma once

// Core tier (full)
#include "PCGExCorePCH.h"

// Filters -- base classes used by virtually all nodes
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExClusterFilter.h"

// Blending -- config & sampling types
#include "Details/PCGExBlendingDetails.h"
#include "Sampling/PCGExSamplingCommon.h"

// Foundations -- base processor
#include "Core/PCGExPointsProcessor.h"
