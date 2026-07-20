// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Details/Collections/PCGExAssetCollectionEditor.h"

/**
 * Editor toolkit for Omni collections. The base editor is already fully heterogeneous
 * (per-row registry tile pickers, mixed grid, type-chooser add) -- this subclass only adds
 * Omni-specific toolbar affordances. Same tab set as the base, so the base layout is shared.
 */
class PCGEXCOLLECTIONSEDITOR_API FPCGExOmniCollectionEditor : public FPCGExAssetCollectionEditor
{
public:
	virtual FName GetToolkitFName() const override
	{
		return FName("PCGExOmniCollectionEditor");
	}

	virtual FText GetBaseToolkitName() const override
	{
		return INVTEXT("PCGEx Omni Collection Editor");
	}

protected:
	/** Base toolbar + "Cleanup Type Setup" (remove globals blocks / machinery states whose
	 *  entry type left the collection -- user-triggered counterpart of the automatic setup). */
	virtual void BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder) override;
};
