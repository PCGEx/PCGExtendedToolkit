// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersDecomp.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersDecompModule"

void FPCGExElementsClustersDecompModule::StartupModule()
{
	OldBaseModules.Add(TEXT("PCGExElementsClusters"));
	IPCGExLegacyModuleInterface::StartupModule();
}

void FPCGExElementsClustersDecompModule::ShutdownModule()
{
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersDecompModule, PCGExElementsClustersDecomp)
