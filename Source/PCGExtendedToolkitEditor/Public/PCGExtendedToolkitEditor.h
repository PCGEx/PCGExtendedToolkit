// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Styling/SlateStyle.h"

class FPCGExtendedToolkitEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	TSharedPtr<FSlateStyleSet> Style;
	bool bDataVisualizationRegistered = false;
};
