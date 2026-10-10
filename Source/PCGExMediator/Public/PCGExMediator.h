// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExModuleInterface.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;

/**
 * JSON authoring hub: diagnostics, value dialect, envelope, format and binding registries, transport and the
 * console commands over them. Knows no domain -- domain editor modules register their models from their own
 * StartupModule. See workspace .claude/Mediator_Architecture.md.
 */
class FPCGExMediatorModule final : public IPCGExModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<IConsoleObject*> ConsoleCommands;
};
