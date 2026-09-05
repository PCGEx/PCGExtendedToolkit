// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditorSettings.h"

#include "CoreMinimal.h"
#include "PCGExVersion.h"
#include "Components/SlateWrapperTypes.h"

FSimpleMulticastDelegate UPCGExCollectionsEditorSettings::OnHiddenAssetPropertyNamesChanged;

void UPCGExCollectionsEditorSettings::PostLoad()
{
	Super::PostLoad();
}

void UPCGExCollectionsEditorSettings::ToggleHiddenAssetPropertyName(const FName PropertyName, const bool bHide)
{
	if (bHide)
	{
		bool bIsAlreadyInSet = false;
		HiddenPropertyNames.Add(PropertyName, &bIsAlreadyInSet);
		if (bIsAlreadyInSet)
		{
			return;
		}
	}
	else if (!HiddenPropertyNames.Remove(PropertyName))
	{
		return;
	}

	SaveConfig();
	OnHiddenAssetPropertyNamesChanged.Broadcast();
}

void UPCGExCollectionsEditorSettings::ToggleHiddenAssetPropertyName(const TArray<FName> Properties, const bool bHide)
{
	if (Properties.IsEmpty())
	{
		return;
	}
	bool bAnyChanges = false;
	if (bHide)
	{
		for (FName PropertyName : Properties)
		{
			bool bIsAlreadyInSet = false;
			HiddenPropertyNames.Add(PropertyName, &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				bAnyChanges = true;
			}
		}
	}
	else
	{
		for (FName PropertyName : Properties)
		{
			if (HiddenPropertyNames.Remove(PropertyName))
			{
				bAnyChanges = true;
			}
		}
	}

	if (!bAnyChanges)
	{
		return;
	}

	SaveConfig();
	OnHiddenAssetPropertyNamesChanged.Broadcast();
}

EVisibility UPCGExCollectionsEditorSettings::GetPropertyVisibility(const FName PropertyName) const
{
	const FName* Id = PropertyNamesMap.Find(PropertyName);
	return Id ? HiddenPropertyNames.Contains(*Id) ? EVisibility::Collapsed : EVisibility::Visible : EVisibility::Visible;
}

bool UPCGExCollectionsEditorSettings::GetIsPropertyVisible(const FName PropertyName) const
{
	return !HiddenPropertyNames.Contains(PropertyName);
}
