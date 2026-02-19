// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"

struct FGraphPanelPinFactory;
struct FGraphPanelNodeFactory;

class FPCGExElementsValencyEditorModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Pin factory for diamond-shaped Root pins in connector pattern graph */
	TSharedPtr<FGraphPanelPinFactory> PatternRootPinFactory;

	/** Node factory that places RootIn pin in the title bar of entry nodes */
	TSharedPtr<FGraphPanelNodeFactory> PatternEntryNodeFactory;
};
