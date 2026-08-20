// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExLegacyModuleInterface.h"

class FPCGExElementsClustersSketchModule final : public IPCGExLegacyModuleInterface
{
	PCGEX_MODULE_BODY

public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
