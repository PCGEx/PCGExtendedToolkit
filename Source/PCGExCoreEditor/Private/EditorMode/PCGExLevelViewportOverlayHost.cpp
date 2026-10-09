// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExLevelViewportOverlayHost.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"

TSharedPtr<SLevelViewport> FPCGExLevelViewportOverlayHost::ResolveHoveredLevelViewport(const FEditorModeTools* ModeManager)
{
	const FEditorViewportClient* HoveredClient = ModeManager ? ModeManager->GetHoveredViewportClient() : nullptr;
	if (!HoveredClient || !GEditor)
	{
		return nullptr;
	}

	// The downcast needs the level-viewport claim PROVABLE: membership in the typed client array is it.
	for (const FLevelEditorViewportClient* LevelClient : GEditor->GetLevelViewportClients())
	{
		if (LevelClient == HoveredClient)
		{
			return StaticCastSharedPtr<SLevelViewport>(HoveredClient->GetEditorViewportWidget());
		}
	}
	return nullptr;
}

bool FPCGExLevelViewportOverlayHost::Update(const FEditorModeTools* ModeManager, const TSharedRef<SWidget>& InWidget)
{
	const TSharedPtr<SLevelViewport> LevelViewport = ResolveHoveredLevelViewport(ModeManager);
	if (!LevelViewport.IsValid())
	{
		return false; // nothing hovered (the cursor may be on the overlay itself): keep the current host
	}

	if (Hosted == InWidget && Viewport.Pin() == LevelViewport)
	{
		return false;
	}

	Remove();
	LevelViewport->AddOverlayWidget(InWidget);
	Hosted = InWidget;
	Viewport = LevelViewport;
	return true;
}

void FPCGExLevelViewportOverlayHost::Remove()
{
	if (const TSharedPtr<SLevelViewport> Pinned = Viewport.Pin(); Pinned.IsValid() && Hosted.IsValid())
	{
		Pinned->RemoveOverlayWidget(Hosted.ToSharedRef());
	}
	Hosted.Reset();
	Viewport.Reset();
}

bool FPCGExLevelViewportOverlayHost::IsHostedOn(const FEditorViewportClient* Client) const
{
	const TSharedPtr<SLevelViewport> Pinned = Viewport.Pin();
	return Client && Pinned.IsValid() && Hosted.IsValid() && Pinned->GetViewportClient().Get() == Client;
}

TSharedPtr<SLevelViewport> FPCGExLevelViewportOverlayHost::GetViewport() const
{
	return Hosted.IsValid() ? Viewport.Pin() : nullptr;
}
