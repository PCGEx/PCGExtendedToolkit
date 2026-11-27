// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "ToolMenus.h"
#include "AssetRegistry/AssetData.h"

class UContentBrowserAssetContextMenuContext;

namespace PCGExCollectionsEditorMenuUtils
{
	FToolMenuSection& CreatePCGExSection(UToolMenu* Menu);

	/** Adds the PCGEx entries of the content browser's Asset Actions submenu that apply to the selection in Context. */
	void ExtendAssetActionsMenu(UToolMenu* Menu, const UContentBrowserAssetContextMenuContext& Context);

	bool DoesAssetInheritFromAActor(const FAssetData& AssetData);
}
