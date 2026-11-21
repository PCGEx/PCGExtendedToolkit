// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExLevelCollectionEditor.h"

#include "Engine/World.h"

const UClass* FPCGExLevelCollectionEditor::GetTilePickerAllowedClass() const
{
	return UWorld::StaticClass();
}
