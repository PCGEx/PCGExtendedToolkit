// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UPCGExValencyCageEditorMode;
class APCGExValencyCageBase;
class UPCGExValencyCageConnectorComponent;

/**
 * Tabbed control panel with Connectors / Assets / Placement tabs.
 * Also provides a connector detail dive-in view (index 3 in the widget switcher).
 *
 * Hidden when a volume or palette is selected (ModuleInfo handles those).
 * Preserves tab index, search filter, and detail panel connector across rebuilds.
 */
class SValencyControlTabs : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencyControlTabs) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Root content area (hidden when volume/palette selected) */
	TSharedPtr<SBox> RootArea;

	/** Active tab index (0=Connectors, 1=Assets, 2=Placement) */
	int32 ActiveTabIndex = 0;

	/** Whether we're showing the connector detail dive-in */
	bool bShowingConnectorDetail = false;

	/** Persisted search filter text for connector lists */
	FString ConnectorSearchFilter;

	/** When set, shows the connector detail panel */
	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> DetailPanelConnector;

	/** Guard: when true, RefreshContent is deferred until selection updates complete */
	bool bIsUpdatingSelection = false;

	/** Delegate handles */
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnComponentSelectionChangedHandle;
	FDelegateHandle OnSceneChangedHandle;

	/** Rebuild content */
	void RefreshContent();

	/** Selection changed callback */
	void OnSelectionChangedCallback(UObject* InObject);

	/** Get the currently selected cage (or nullptr) */
	APCGExValencyCageBase* GetSelectedCage() const;

	/** Build tab bar + content area */
	TSharedRef<SWidget> BuildTabContent(APCGExValencyCageBase* Cage);

	/** Build individual tab contents */
	TSharedRef<SWidget> BuildConnectorsTab(APCGExValencyCageBase* Cage);
	TSharedRef<SWidget> BuildAssetsTab(APCGExValencyCageBase* Cage);
	TSharedRef<SWidget> BuildPlacementTab(APCGExValencyCageBase* Cage);

	/** Build connector detail dive-in view */
	TSharedRef<SWidget> BuildConnectorDetail(UPCGExValencyCageConnectorComponent* Connector);

	/** Build a compact connector row with inline controls */
	TSharedRef<SWidget> MakeCompactConnectorRow(UPCGExValencyCageConnectorComponent* ConnectorComp, bool bIsActive);

	/** Build the Add Connector button */
	TSharedRef<SWidget> MakeAddConnectorButton(APCGExValencyCageBase* Cage);

	/** Build the related section (containing volumes, mirrors, mirrored-by) */
	TSharedRef<SWidget> MakeRelatedSection(APCGExValencyCageBase* Cage);
};
