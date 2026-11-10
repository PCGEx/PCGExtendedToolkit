// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;
class SWidget;

/**
 * Compact inline editor for an FPCGExEnumSelector property handle.
 *
 * Replaces the engine's FEnumSelectorDetails customization, which crashes when its
 * detail panel is rebuilt mid-callstack via ForceRefresh() (its ReloadValueOptions()
 * dereferences a property handle that has just been invalidated).
 *
 * Two render modes are supported via bAllowClassPicker:
 *  - true  (schema-edit): exposes both an enum-class picker AND a value picker.
 *  - false (override / readonly schema): exposes only a value picker; class is fixed.
 *
 * A field tagged meta=(PCGExEnumFlags) holding a Bitflags enum gets a multi-select checkbox menu
 * instead of the single-select combo.
 */
namespace PCGExEnumSelectorWidget
{
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> Make(
		const TSharedRef<IPropertyHandle>& ValueHandle,
		bool bAllowClassPicker);
}
