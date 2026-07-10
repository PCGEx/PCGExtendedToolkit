// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"

class FPCGExPropertiesEditorStyle;

class FPCGExPropertiesEditorModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Slate style set for the inline curve editor widgets (key/gem SVG brushes). */
	TSharedPtr<FPCGExPropertiesEditorStyle> Style;
};
