// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;
class SWidget;

/**
 * Inline editor for FPCGExProperty_Range::Value: a range slider over the stored 0..1 positions, plus a
 * double-click popover with exact fields -- the positions on one row, the output values they remap to
 * (through the property's Min / Max) on the other.
 *
 * One widget for both registry modes: the bounds are edited on their own rows in the schema and are fixed
 * everywhere else, so the value widget never needs definition controls.
 */
namespace PCGExRangePropertyWidget
{
	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle);
}
