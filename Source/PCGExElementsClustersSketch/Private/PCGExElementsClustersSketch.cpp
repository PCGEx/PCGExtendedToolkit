// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersSketch.h"

#include "Collections/PCGExClusterSketchCollection.h"

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersSketchModule"

void FPCGExElementsClustersSketchModule::StartupModule()
{
	IPCGExModuleInterface::StartupModule();

	// Out-of-module collection type: PCGExCollections flushed its registry in its own StartupModule,
	// which runs before this module's static init -- so this cannot ride a static auto-registrar.
	PCGExSketch::RegisterCollectionType();
}

void FPCGExElementsClustersSketchModule::ShutdownModule()
{
	// The registry outlives this module and holds closures compiled into its DLL.
	PCGExAssetCollection::FTypeRegistry::Get().Unregister(PCGExSketch::CollectionTypeId);

	IPCGExModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersSketchModule, PCGExElementsClustersSketch)
