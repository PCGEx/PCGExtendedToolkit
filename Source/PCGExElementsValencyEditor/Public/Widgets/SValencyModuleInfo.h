// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UPCGExValencyCageEditorMode;

/**
 * Context-sensitive module info panel.
 * Shows cage properties, volume info, palette info, or hint text
 * based on the current editor selection.
 * Dispatches to type-specific SValencyInfoPanel subclasses for cages/palettes.
 */
class SValencyModuleInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencyModuleInfo) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Content area rebuilt on selection change */
	TSharedPtr<SBox> ContentArea;

	/** Delegate handles */
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnComponentSelectionChangedHandle;
	FDelegateHandle OnSceneChangedHandle;

	/** Rebuild content based on selection */
	void RefreshContent();

	/** Selection changed callback */
	void OnSelectionChangedCallback(UObject* InObject);

	/** Build content for non-dispatched contexts */
	TSharedRef<SWidget> BuildHintContent();
	TSharedRef<SWidget> BuildVolumeInfoContent(class AValencyContextVolume* Volume);
};
