// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorContentFilter.h"

#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Engine/LevelScriptActor.h"
#include "Engine/Brush.h"
#include "GameFramework/Info.h"
#endif

#pragma region UPCGExActorContentFilter

bool UPCGExActorContentFilter::IsInfrastructureActor(AActor* Actor)
{
	if (!Actor) { return true; }
	if (Actor->IsHidden()) { return true; }
	if (Actor->bIsEditorOnlyActor) { return true; }
	
#if WITH_EDITOR
	if (Actor->bIsMainWorldOnly) { return true; }
	if (Actor->IsA<ALevelScriptActor>()) { return true; }
	if (Actor->IsA<AInfo>()) { return true; }
	if (Actor->IsA<ABrush>()) { return true; }

	// Soft check for ANavigationData — avoids hard link dependency on NavigationSystem module
	static UClass* NavigationDataClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationData"));
	if (NavigationDataClass && Actor->IsA(NavigationDataClass)) { return true; }
#endif

	return false;
}

bool UPCGExActorContentFilter::StaticPassesFilter(
	const UPCGExActorContentFilter* Filter, AActor* Actor,
	UPCGExAssetCollection* OwningCollection, int32 EntryIndex)
{
	if (!Actor) { return false; }

	if (Filter)
	{
		return Filter->PassesFilter(Actor, OwningCollection, EntryIndex);
	}

	return !IsInfrastructureActor(Actor);
}

bool UPCGExActorContentFilter::PassesFilter_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	return !IsInfrastructureActor(Actor);
}

#pragma endregion

#pragma region UPCGExDefaultActorContentFilter

bool UPCGExDefaultActorContentFilter::PassesFilter_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const
{
	if (IsInfrastructureActor(Actor)) { return false; }

	// Tag include filter
	if (IncludeTags.Num() > 0)
	{
		bool bHasIncludeTag = false;
		for (const FName& Tag : IncludeTags)
		{
			if (Actor->Tags.Contains(Tag)) { bHasIncludeTag = true; break; }
		}
		if (!bHasIncludeTag) { return false; }
	}

	// Tag exclude filter
	if (ExcludeTags.Num() > 0)
	{
		for (const FName& Tag : ExcludeTags)
		{
			if (Actor->Tags.Contains(Tag)) { return false; }
		}
	}

	// Class include filter
	if (IncludeClasses.Num() > 0)
	{
		bool bMatchesClass = false;
		for (const TSoftClassPtr<AActor>& ClassPtr : IncludeClasses)
		{
			if (UClass* C = ClassPtr.Get()) { if (Actor->IsA(C)) { bMatchesClass = true; break; } }
		}
		if (!bMatchesClass) { return false; }
	}

	// Class exclude filter
	if (ExcludeClasses.Num() > 0)
	{
		for (const TSoftClassPtr<AActor>& ClassPtr : ExcludeClasses)
		{
			if (UClass* C = ClassPtr.Get()) { if (Actor->IsA(C)) { return false; } }
		}
	}

	return true;
}

#pragma endregion
