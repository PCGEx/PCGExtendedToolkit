// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditorSettings.h"

#include "CoreMinimal.h"
#include "PCGExVersion.h"

FSimpleMulticastDelegate UPCGExCollectionsEditorSettings::OnHiddenCategoryGroupsChanged;

void UPCGExCollectionsEditorSettings::PostLoad()
{
	Super::PostLoad();
}

void UPCGExCollectionsEditorSettings::SetHideIncompatiblePickerEntries(const bool bHide)
{
	if (bHideIncompatiblePickerEntries == bHide)
	{
		return;
	}
	bHideIncompatiblePickerEntries = bHide;
	SaveConfig();
}

bool UPCGExCollectionsEditorSettings::SetCategoryGroupHidden(const FName Group, const bool bHidden)
{
	return SetCategoryGroupsHidden(MakeArrayView(&Group, 1), bHidden);
}

bool UPCGExCollectionsEditorSettings::SetCategoryGroupsHidden(const TConstArrayView<FName> Groups, const bool bHidden)
{
	bool bChanged = false;
	for (const FName Group : Groups)
	{
		if (Group.IsNone())
		{
			continue;
		}

		if (bHidden)
		{
			bool bAlreadyIn = false;
			HiddenCategoryGroups.Add(Group, &bAlreadyIn);
			bChanged |= !bAlreadyIn;
		}
		else
		{
			bChanged |= HiddenCategoryGroups.Remove(Group) > 0;
		}
	}

	if (!bChanged)
	{
		return false;
	}

	SaveConfig();
	OnHiddenCategoryGroupsChanged.Broadcast();
	return true;
}
