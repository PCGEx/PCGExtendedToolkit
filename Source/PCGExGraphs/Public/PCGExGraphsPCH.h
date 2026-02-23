// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Tier 3 shared PCH -- extends Foundations PCH with Clusters and Graph headers.
// Consumed by all modules that depend on PCGExGraphs (~13 modules).

#pragma once

// Foundations tier (full)
#include "PCGExFoundationsPCH.h"

// Clusters -- processor, batch templates, common types
#include "Core/PCGExClustersProcessor.h"
