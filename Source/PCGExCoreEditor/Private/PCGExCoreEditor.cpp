// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCoreEditor.h"

#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Details/PCGExDotComparisonCustomization.h"

#define LOCTEXT_NAMESPACE "FPCGExCoreEditorModule"

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExCoreEditorModule, PCGExCoreEditor)

void FPCGExCoreEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExStaticDotComparisonDetails", FPCGExDotComparisonCustomization)
	PCGEX_REGISTER_CUSTO("PCGExDotComparisonDetails", FPCGExDotComparisonCustomization)
}

void FPCGExCoreEditorModule::ShutdownModule()
{
	IPCGExEditorModuleInterface::ShutdownModule();
}
