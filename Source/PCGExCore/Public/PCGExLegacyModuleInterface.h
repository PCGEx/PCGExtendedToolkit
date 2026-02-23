// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExModuleInterface.h"
#include "Modules/ModuleManager.h"

class PCGEXCORE_API IPCGExLegacyModuleInterface : public IPCGExModuleInterface
{
public:
	IPCGExLegacyModuleInterface()
	{
#if PCGEX_SUBMODULE_CORE_REDIRECT_ENABLED
		OldBaseModules.Add(TEXT("PCGExtendedToolkit"));
#endif
	}
};
