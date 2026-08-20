// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersSketch.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersSketchModule"

void FPCGExElementsClustersSketchModule::StartupModule()
{
	IPCGExLegacyModuleInterface::StartupModule();
}

void FPCGExElementsClustersSketchModule::ShutdownModule()
{
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersSketchModule, PCGExElementsClustersSketch)
