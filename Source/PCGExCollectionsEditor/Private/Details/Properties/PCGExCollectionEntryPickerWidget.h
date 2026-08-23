// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;
class SWidget;

/**
 * Inline editor for FPCGExProperty_CollectionEntry::Value (FPCGExCollectionEntryRef): collection asset
 * box, lock toggle, and an entry dropdown with a live thumbnail. Registered with FPCGExInlineWidgetRegistry.
 *
 *  - bSchemaEdit true (schema authoring): collection box + lock toggle + default-entry picker.
 *  - false (override rows): entry picker; the collection box shows only while the schema left it unlocked
 *    (locked rows drop it entirely -- the row title already names the collection via GetDisplayTypeName).
 */
namespace PCGExCollectionEntryPickerWidget
{
	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, bool bSchemaEdit);
}
