// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExModuleInterface.h" // Boilerplate
#include "Modules/ModuleManager.h"

class FPCGDataTypeRegistry;

class PCGEXCOREEDITOR_API IPCGExEditorModuleInterface : public IModuleInterface
{
public:
	virtual FString GetModuleName() const
	{
		return TEXT("");
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void RegisterMenuExtensions();
	virtual void UnregisterMenuExtensions();
};
