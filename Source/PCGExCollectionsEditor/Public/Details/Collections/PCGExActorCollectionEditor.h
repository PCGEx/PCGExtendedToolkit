// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

class SVerticalBox;

#include "PCGExAssetCollectionEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

/** Actor collection editor -- reference implementation with custom add-menu content. */
class FPCGExActorCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExActorCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Actor Collection Editor");
	}

protected:
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;
	virtual void BuildAddMenuContent(const TSharedRef<SVerticalBox>& MenuBox) override;

	virtual FName GetTilePickerPropertyName() const override
	{
		return FName("Actor");
	}

	virtual const UClass* GetTilePickerAllowedClass() const override;
};
