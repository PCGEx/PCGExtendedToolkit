// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersDiagrams.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersDiagramsModule"

void FPCGExElementsClustersDiagramsModule::StartupModule()
{
	OldBaseModules.Add(TEXT("PCGExElementsClusters"));
	IPCGExLegacyModuleInterface::StartupModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersDiagramsModule, PCGExElementsClustersDiagrams)
