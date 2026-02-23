// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExAddonModuleInterface.h"

void IPCGExAddonModuleInterface::StartupModule()
{
	IPCGExModuleInterface::StartupModule();

#if WITH_EDITOR
	SelfRegisterToEditor();
#endif
}

void IPCGExAddonModuleInterface::ShutdownModule()
{
	IPCGExModuleInterface::ShutdownModule();
}

#if WITH_EDITOR
void IPCGExAddonModuleInterface::SelfRegisterToEditor()
{
	TSharedPtr<FSlateStyleSet> Style = EditorStyle.Pin();
	if (Style.IsValid())
	{
		RegisterToEditor(Style);
		RegisterMenuExtensions();
	}
}
#endif
