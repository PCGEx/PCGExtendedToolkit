// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCoreEditor.h"

#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Details/PCGExAttributeSourceToTargetCustomization.h"
#include "Details/PCGExDotComparisonCustomization.h"
#include "Details/PCGExNameFiltersCustomization.h"

PCGEX_IMPLEMENT_MODULE(FPCGExCoreEditorModule, PCGExCoreEditor)

void FPCGExCoreEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExStaticDotComparisonDetails", FPCGExDotComparisonCustomization)
	PCGEX_REGISTER_CUSTO("PCGExDotComparisonDetails", FPCGExDotComparisonCustomization)
	PCGEX_REGISTER_CUSTO("PCGExNameFiltersDetails", FPCGExNameFiltersDetailsCustomization)
	PCGEX_REGISTER_CUSTO("PCGExAttributeGatherDetails", FPCGExNameFiltersDetailsCustomization)
	PCGEX_REGISTER_CUSTO("PCGExForwardDetails", FPCGExForwardDetailsCustomization)
	PCGEX_REGISTER_CUSTO("PCGExAttributeSourceToTargetDetails", FPCGExAttributeSourceToTargetCustomization)
}
