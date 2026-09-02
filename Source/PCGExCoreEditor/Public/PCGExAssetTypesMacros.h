// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define PCGEX_REGISTER_CUSTO_START FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
#define PCGEX_REGISTER_CUSTO(_NAME, _CLASS) PropertyModule.RegisterCustomPropertyTypeLayout(_NAME, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&_CLASS::MakeInstance));
