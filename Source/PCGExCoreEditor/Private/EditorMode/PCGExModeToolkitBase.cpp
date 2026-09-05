// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExModeToolkitBase.h"

#include "Toolkits/AssetEditorModeUILayer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"

TSharedPtr<SWidget> FPCGExModeToolkitBase::GetInlineContent() const
{
	return nullptr;
}

void FPCGExModeToolkitBase::RequestModeUITabs()
{
	FModeToolkit::RequestModeUITabs();

	// Same tab id, own spawner: this replaces the registration the base just made.
	if (const TSharedPtr<FAssetEditorModeUILayer> Layer = ModeUILayer.Pin())
	{
		PrimaryTabInfo.OnSpawnTab = FOnSpawnTab::CreateSP(SharedThis(this), &FPCGExModeToolkitBase::SpawnPrimaryTab);
		Layer->SetModePanelInfo(UAssetEditorUISubsystem::TopLeftTabID, PrimaryTabInfo);
	}
}

TSharedRef<SDockTab> FPCGExModeToolkitBase::SpawnPrimaryTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> Tab = CreatePrimaryModePanel(Args);

	// The holder's visibility is bound to "content is not the null widget", so filling it also shows it.
	if (InlineContentHolder.IsValid())
	{
		if (const TSharedPtr<SWidget> Fill = MakeFillContent())
		{
			InlineContentHolder->SetContent(Fill.ToSharedRef());
		}
	}

	DecoratePrimaryTab(Tab);
	return Tab;
}
