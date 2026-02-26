// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersEditor.h"

#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Details/PCGExAdjacencySettingsCustomization.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersEditorModule"

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersEditorModule, PCGExElementsClustersEditor)

void FPCGExElementsClustersEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExAdjacencySettings", FPCGExAdjacencySettingsCustomization)
}

void FPCGExElementsClustersEditorModule::ShutdownModule()
{
	IPCGExEditorModuleInterface::ShutdownModule();
}
