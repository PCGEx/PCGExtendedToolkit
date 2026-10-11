// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/StructView.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"

class IPropertyHandle;
class SComboButton;
struct FPCGExProperty;

/**
 * Choice dropdown for a property row. Host aliases the property struct the row edits (same lifetime as the row's
 * struct-on-scope) and is re-read every paint, so a sync never leaves the widget stale. Locked: the button shows the
 * current choice, or "Custom" with the value's preview, and stands in for the value editor. Unlocked: a bare arrow
 * beside it. A pick goes through NotifyHandle's pre/post-change bracket, so the host's dirty and refresh hooks fire
 * exactly as for a typed edit.
 */
class SPCGExPropertyChoicePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyChoicePicker)
			: _Locked(false)
		{
		}

		SLATE_ARGUMENT(bool, Locked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FStructView InHost, const TSharedRef<IPropertyHandle>& InNotifyHandle);

private:
	const FPCGExProperty* GetHost() const;
	int32 GetSelectedIndex() const;
	/** Index of the choice the host is bound to (its live pick), INDEX_NONE for a custom value. */
	int32 GetBoundIndex() const;
	FText GetSelectionLabel() const;
	FText GetSelectionPreview() const;
	FSlateColor GetSelectionColor() const;
	/** The arrow: accent blue while bound, the style's foreground otherwise. */
	FSlateColor GetArrowColor() const;
	FText GetSelectionToolTip() const;
	TSharedRef<SWidget> BuildMenu();
	void PickChoice(FGuid ChoiceId);

	FStructView Host;
	TSharedPtr<IPropertyHandle> NotifyHandle;
	bool bLocked = false;
	TSharedPtr<SComboButton> ComboButton;
};
