// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExRandomRatioFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExRandomRatioFilterFactory::SupportsCollectionEvaluation() const
{
	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExRandomRatioFilterFactory::CreateFilter() const
{
	PCGEX_MAKE_SHARED(Filter, PCGExPointFilter::FRandomRatioFilter, this)
	return Filter;
}

#if WITH_EDITOR
void UPCGExRandomRatioFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 0)
	{
		Config.Random.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExRandomRatioFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 0)
	{
		Config.Random.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

void PCGExPointFilter::FRandomRatioFilter::BuildCollectionPicks(const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	FScopeLock Lock(&CollectionPicksLock);
	if (bCollectionPicksBuilt.load(std::memory_order_relaxed))
	{
		return;
	}

	// One draw covers every dataset on the pin; attribute-driven inputs read the first dataset's @Data.
	const TSharedPtr<PCGExData::FPointIO> DrawSource = (*ParentCollection)[0];

	TSet<int32> Picks;
	bCollectionPicksValid = TypedFilterFactory->Config.Random.GetPicks(DrawSource->GetContext(), DrawSource->GetIn(), ParentCollection->Num(), Picks, PCGEX_QUIET_HANDLING);
	CollectionPicks = MoveTemp(Picks);
	BuiltForCollection = ParentCollection.Get();

	bCollectionPicksBuilt.store(true, std::memory_order_release);
}

bool PCGExPointFilter::FRandomRatioFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}
	if (!bWillBeUsedWithCollections)
	{
		TypedFilterFactory->Config.Random.GetPicks(InContext, InPointDataFacade->GetIn(), InPointDataFacade->GetNum(), PointPicks);
	}
	return true;
}

bool PCGExPointFilter::FRandomRatioFilter::Test(const int32 PointIndex) const
{
	return TypedFilterFactory->Config.bInvertResult ? !PointPicks.Contains(PointIndex) : PointPicks.Contains(PointIndex);
}

bool PCGExPointFilter::FRandomRatioFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	if (!ParentCollection || ParentCollection->IsEmpty())
	{
		return false;
	}

	if (!bCollectionPicksBuilt.load(std::memory_order_acquire))
	{
		BuildCollectionPicks(ParentCollection);
	}

	ensureMsgf(BuiltForCollection == ParentCollection.Get(), TEXT("A Random Ratio filter instance must only test datasets from a single collection."));

	if (!bCollectionPicksValid)
	{
		PCGEX_QUIET_HANDLING_RET
	}

	const bool bPicked = CollectionPicks.Contains(IO->IOIndex);
	return TypedFilterFactory->Config.bInvertResult ? !bPicked : bPicked;
}

PCGEX_CREATE_FILTER_FACTORY(RandomRatio)

#if WITH_EDITOR
FString UPCGExRandomRatioFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(TEXT("Random Ratio"), Config.bInvertResult);
}
#endif


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
