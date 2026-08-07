// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorContentFilter.h"

#include "PCGExCollectionsCommon.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExSocketProvider.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Engine/Brush.h"
#include "Engine/LevelScriptActor.h"
#include "GameFramework/Info.h"
#include "GameFramework/Volume.h"
#endif

#pragma region UPCGExActorContentFilter

TSet<FName> UPCGExActorContentFilter::KnownSystemActorClasses =
{
	// Add new entries here as they are discovered; external plugins call RegisterSystemActorClass().
	TEXT("ChaosDebugDrawActor"),
	TEXT("PCGWorldActor"),
	TEXT("GameplayDebuggerPlayerManager"),
	TEXT("BuoyancyManager"),
	TEXT("ValencyEditorCache"),
	TEXT("PCGExValencyModuleCage"),
};

bool UPCGExActorContentFilter::IsInfrastructureActor(const AActor* Actor)
{
	if (!Actor)
	{
		return true;
	}
	if (Actor->IsHidden())
	{
		return true;
	}
	if (Actor->bIsEditorOnlyActor)
	{
		return true;
	}

#if WITH_EDITOR
	if (Actor->bIsMainWorldOnly)
	{
		return true;
	}
	if (Actor->IsA<ALevelScriptActor>())
	{
		return true;
	}
	if (Actor->IsA<AInfo>())
	{
		return true;
	}
	if (Actor->IsA<ABrush>() && !Actor->IsA<AVolume>())
	{
		return true;
	}

	// Soft check for ANavigationData -- avoids hard link dependency on NavigationSystem module
	static UClass* NavigationDataClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationData"));
	if (NavigationDataClass && Actor->IsA(NavigationDataClass))
	{
		return true;
	}

	// Match by class name (e.g. PCGWorldActor) OR actor instance name (e.g. ChaosDebugDrawActor,
	// which is a bare AActor instance with no dedicated C++ class of its own).
	// KnownSystemActorClasses is a C++ static initializer and is always populated; the settings
	// cache is the combined set but may be empty if IsInfrastructureActor is called before PostLoad.
	{
		const FName ClassName = Actor->GetClass()->GetFName();
		const FName ActorName = Actor->GetFName();
		if (KnownSystemActorClasses.Contains(ClassName) || KnownSystemActorClasses.Contains(ActorName) ||
			PCGEX_COLLECTIONS_SETTINGS.SystemActorClasses.Contains(ClassName) || PCGEX_COLLECTIONS_SETTINGS.SystemActorClasses.Contains(ActorName))
		{
			return true;
		}
	}
#endif

	return false;
}

void UPCGExActorContentFilter::RegisterSystemActorClass(FName ClassName)
{
	KnownSystemActorClasses.Add(ClassName);
	// Push into the cache so the addition takes effect immediately, even if external plugins
	// register before/outside the developer-settings PostEdit flow.
	PCGEX_COLLECTIONS_SETTINGS.SystemActorClasses.Add(ClassName);
}

bool UPCGExActorContentFilter::StaticPassesFilter(
	const UPCGExActorContentFilter* Filter, AActor* Actor,
	UPCGExAssetCollection* OwningCollection, int32 EntryIndex)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->ActorHasTag(PCGExCollections::Labels::StaticIgnore))
	{
		return false;
	}
	
	// Socket providers that strip themselves are excluded from all content scans
	if (const IPCGExSocketProvider* Provider = Cast<IPCGExSocketProvider>(Actor))
	{
		if (Provider->ShouldStripFromExport_Implementation())
		{
			return false;
		}
	}

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
	if (IsInfrastructureActor(Actor))
	{
		return false;
	}

	// Tag include filter
	if (IncludeTags.Num() > 0)
	{
		bool bHasIncludeTag = false;
		for (const FName& Tag : IncludeTags)
		{
			if (Actor->Tags.Contains(Tag))
			{
				bHasIncludeTag = true;
				break;
			}
		}
		if (!bHasIncludeTag)
		{
			return false;
		}
	}

	// Tag exclude filter
	if (ExcludeTags.Num() > 0)
	{
		for (const FName& Tag : ExcludeTags)
		{
			if (Actor->Tags.Contains(Tag))
			{
				return false;
			}
		}
	}

	// Class include filter
	if (IncludeClasses.Num() > 0)
	{
		bool bMatchesClass = false;
		for (const TSoftClassPtr<AActor>& ClassPtr : IncludeClasses)
		{
			if (UClass* C = ClassPtr.Get())
			{
				if (Actor->IsA(C))
				{
					bMatchesClass = true;
					break;
				}
			}
		}
		if (!bMatchesClass)
		{
			return false;
		}
	}

	// Class exclude filter
	if (ExcludeClasses.Num() > 0)
	{
		for (const TSoftClassPtr<AActor>& ClassPtr : ExcludeClasses)
		{
			if (UClass* C = ClassPtr.Get())
			{
				if (Actor->IsA(C))
				{
					return false;
				}
			}
		}
	}

	return true;
}

#pragma endregion
