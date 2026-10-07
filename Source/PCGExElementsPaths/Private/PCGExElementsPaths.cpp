// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsPaths.h"

#include "Helpers/PCGExSplineSampleBuiltinHandlers.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsPathsModule"

void FPCGExElementsPathsModule::StartupModule()
{
	IPCGExLegacyModuleInterface::StartupModule();
	PCGExSplineSampling::RegisterBuiltinHandlers();
}

void FPCGExElementsPathsModule::ShutdownModule()
{
	PCGExSplineSampling::UnregisterBuiltinHandlers();
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsPathsModule, PCGExElementsPaths)
