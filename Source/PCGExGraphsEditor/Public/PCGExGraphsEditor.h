// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"

class FPCGExGraphsEditorModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** UThumbnailManager is only safe to touch once the engine is up; see StartupModule. */
	void RegisterThumbnailRenderer();

	FDelegateHandle OnPostEngineInitHandle;
	bool bThumbnailRendererRegistered = false;
};
