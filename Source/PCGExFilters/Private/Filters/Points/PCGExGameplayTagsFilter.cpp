// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExGameplayTagsFilter.h"

#include "PCGExLog.h"
#include "PropertyPathHelpers.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExGameplayTagsFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FGameplayTagsFilter>(this);
}

bool UPCGExGameplayTagsFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	PCGEX_VALIDATE_NAME_CONSUMABLE_C(InContext, Config.ActorReference)

	return true;
}

bool PCGExPointFilter::FGameplayTagsFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	PropertyPath = FCachedPropertyPath(TypedFilterFactory->Config.PropertyPath);
	if (!PropertyPath.IsValid())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid PropertyPath."));
		return false;
	}

	PathSegments.Reserve(PropertyPath.GetNumSegments());
	for (int i = 0; i < PropertyPath.GetNumSegments(); i++)
	{
		PathSegments.Add(PropertyPath.GetSegment(i).Name.ToString());
	}

	// Full read, so each unique path is resolved once here (GC-guarded: filters init on workers) instead of per Test.
	const TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> ActorReferences = PointDataFacade->GetBroadcaster<FSoftObjectPath>(TypedFilterFactory->Config.ActorReference, false);
	if (!ActorReferences)
	{
		PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Actor Reference, TypedFilterFactory->Config.ActorReference)
		return false;
	}

	const int32 NumPoints = PointDataFacade->GetNum();
	TMap<FSoftObjectPath, int32> UniqueIndex;
	TArray<FSoftObjectPath> UniquePaths;
	ActorIndices.Init(INDEX_NONE, NumPoints);

	for (int32 i = 0; i < NumPoints; i++)
	{
		const FSoftObjectPath& Path = ActorReferences->Read(i);
		if (Path.IsNull())
		{
			continue;
		}
		int32& Index = UniqueIndex.FindOrAdd(Path, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = UniquePaths.Add(Path);
		}
		ActorIndices[i] = Index;
	}

	TArray<UObject*> Resolved;
	PCGExHelpers::ResolveObjects_AnyThread(UniquePaths, Resolved);

	Actors.Reserve(Resolved.Num());
	for (UObject* Object : Resolved)
	{
		Actors.Add(Cast<AActor>(Object));
	}

	return true;
}

bool PCGExPointFilter::FGameplayTagsFilter::Test(const int32 PointIndex) const
{
	const int32 ActorIndex = ActorIndices[PointIndex];
	AActor* TargetActor = ActorIndex != INDEX_NONE ? Actors[ActorIndex].Get() : nullptr;
	if (!TargetActor)
	{
		return TypedFilterFactory->Config.bFallbackMissingActor;
	}

	const FCachedPropertyPath Path = FCachedPropertyPath(PathSegments);
	FGameplayTagContainer TagContainer;
	FProperty* Property = nullptr;

	if (!PropertyPathHelpers::GetPropertyValue(TargetActor, Path, TagContainer, Property))
	{
		if (!TypedFilterFactory->Config.bQuietMissingPropertyWarning)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("GameplayTags filter could not resolve target property : \"%s\"."), *TypedFilterFactory->Config.PropertyPath);
		}
		return TypedFilterFactory->Config.bFallbackPropertyPath;
	}
	return TypedFilterFactory->Config.TagQuery.Matches(TagContainer);
}

PCGEX_CREATE_FILTER_FACTORY(GameplayTags)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
