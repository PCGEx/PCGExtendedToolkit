// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UPCGExValencyCageEditorMode;

/**
 * Compact horizontal bar replacing the full scene overview.
 * Shows a Rebuild All button and a Parent Context(s) dropdown
 * populated from the selected cage's containing volumes.
 */
class SValencySceneBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencySceneBar) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Content area rebuilt on selection/scene change */
	TSharedPtr<SBox> ContentArea;

	/** Delegate handles */
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnComponentSelectionChangedHandle;
	FDelegateHandle OnSceneChangedHandle;

	/** Rebuild the bar content */
	void RefreshContent();

	/** Selection changed callback */
	void OnSelectionChangedCallback(UObject* InObject);
};
