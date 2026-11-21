// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExEditorModuleInterface.h"
#include "Editor.h"
#include "PCGExLog.h"
#include "ToolMenus.h"
#include "Styling/SlateStyle.h"

void IPCGExEditorModuleInterface::StartupModule()
{
	UE_LOG(LogPCGEx, Log, TEXT("IPCGExEditorModuleInterface::StartupModule >> %s"), *GetModuleName());
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &IPCGExEditorModuleInterface::RegisterMenuExtensions));
}

void IPCGExEditorModuleInterface::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UnregisterMenuExtensions();
}

void IPCGExEditorModuleInterface::RegisterMenuExtensions()
{
}

void IPCGExEditorModuleInterface::UnregisterMenuExtensions()
{
	UToolMenus::UnregisterOwner(this);
}
