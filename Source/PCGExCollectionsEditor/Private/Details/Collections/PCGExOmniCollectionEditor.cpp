// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExOmniCollectionEditor.h"

#include "ScopedTransaction.h"
#include "Collections/PCGExOmniCollection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

void FPCGExOmniCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
	FPCGExAssetCollectionEditor::BuildAssetHeaderToolbar(ToolbarBuilder);

	ToolbarBuilder.BeginSection("OmniSection");
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UPCGExOmniCollection* Collection = Cast<UPCGExOmniCollection>(EditedCollection.Get());
					if (!Collection)
					{
						return;
					}

					int32 Removed = 0;
					{
						FScopedTransaction Transaction(INVTEXT("Cleanup Unused Type Setup"));
						Removed = Collection->EDITOR_CleanupUnusedTypeSetup();
					}

					FNotificationInfo Info(Removed > 0
						                       ? FText::Format(INVTEXT("Removed {0} unused type setup item(s)."), Removed)
						                       : FText(INVTEXT("No unused type setup to remove.")));
					Info.ExpireDuration = 4.0f;
					Info.bFireAndForget = true;
					FSlateNotificationManager::Get().AddNotification(Info);
				})),
			NAME_None,
			FText::FromString(TEXT("Cleanup")),
			INVTEXT("Remove type globals blocks and machinery states whose entry type is no longer present in the collection. Setup for types still in use -- or unknown to the registry -- is kept."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"));
	}
	ToolbarBuilder.EndSection();
}
