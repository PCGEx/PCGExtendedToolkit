// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsSampling.h"

#include "Core/PCGExBlueNoiseTables.h"

#if WITH_EDITOR
#include "Core/PCGExTexParamFactoryProvider.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGExElementsSamplingModule"

void FPCGExElementsSamplingModule::StartupModule()
{
	IPCGExLegacyModuleInterface::StartupModule();
	PCGExBlueNoise::PrebuildTables();
}

void FPCGExElementsSamplingModule::ShutdownModule()
{
	PCGExBlueNoise::WaitForPrebuild();
	IPCGExLegacyModuleInterface::ShutdownModule();
}

#if WITH_EDITOR
void FPCGExElementsSamplingModule::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
	IPCGExModuleInterface::RegisterToEditor(InStyle);

	PCGEX_START_PCG_REGISTRATION
	PCGEX_REGISTER_DATA_TYPE(TexParam, TexParam)
}
#endif

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsSamplingModule, PCGExElementsSampling)
