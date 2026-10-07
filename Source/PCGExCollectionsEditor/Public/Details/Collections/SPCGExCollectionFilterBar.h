// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "Details/Collections/PCGExCollectionCategoryGroups.h"

/**
 * Footer "Filters" strip: Show all / Hide all plus one toggle per offered group. State lives in
 * UPCGExCollectionsEditorSettings, so every open editor's strip and panels follow a toggle.
 */
class PCGEXCOLLECTIONSEDITOR_API SPCGExCollectionFilterBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExCollectionFilterBar)
		{
		}

		SLATE_ARGUMENT(TArray<PCGExCollectionCategoryGroups::FGroup>, Groups)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TArray<PCGExCollectionCategoryGroups::FGroup> Groups;

	FReply SetAllHidden(bool bHidden);
	FReply ToggleGroup(FName Group);
};
