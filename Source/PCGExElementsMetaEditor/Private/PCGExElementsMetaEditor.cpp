// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsMetaEditor.h"

#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Details/PCGExAttributeToPinCustomization.h"
#include "Details/PCGExIntegerLayoutFieldCustomization.h"

PCGEX_IMPLEMENT_MODULE(FPCGExElementsMetaEditorModule, PCGExElementsMetaEditor)

void FPCGExElementsMetaEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExAttributeToPinDetails", FPCGExAttributeToPinCustomization)
	PCGEX_REGISTER_CUSTO("PCGExIntegerLayoutField", FPCGExIntegerLayoutFieldCustomization)
}
