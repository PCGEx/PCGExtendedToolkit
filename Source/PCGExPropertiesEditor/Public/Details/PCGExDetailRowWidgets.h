// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Misc/Attribute.h"

class SWidget;

namespace PCGExDetailRowWidgets
{
	// Shared "-> [value]" row: a grey arrow followed by an editable text box that shows HintText (greyed) while empty.
	TSharedRef<SWidget> MakeArrowedHintTextBox(
		const TAttribute<FText>& Text,
		const TAttribute<FText>& HintText,
		const FOnTextCommitted& OnCommitted,
		const TAttribute<bool>& IsEnabled = true);
}
