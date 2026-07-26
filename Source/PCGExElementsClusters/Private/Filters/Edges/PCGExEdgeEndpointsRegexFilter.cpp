// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeEndpointsRegexFilter.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeEndpointsRegexFilter"
#define PCGEX_NAMESPACE EdgeEndpointsRegexFilter

void UPCGExEdgeEndpointsRegexFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<FString>(InContext, Config.Attribute);
}

bool UPCGExEdgeEndpointsRegexFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.Attribute, Consumable)

	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeEndpointsRegexFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeEndpointsRegex::FFilter>(this);
}

namespace PCGExEdgeEndpointsRegex
{
	bool FFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		StringBuffer = InPointDataFacade->GetBroadcaster<FString>(TypedFilterFactory->Config.Attribute, false, PCGEX_QUIET_HANDLING);
		if (!StringBuffer)
		{
			PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Comparison Attribute, TypedFilterFactory->Config.Attribute)
			return false;
		}

		// UE's FRegexPattern can't report invalid syntax (a malformed pattern just matches nothing),
		// so there is nothing to validate here.
		RegexMatcher.Compile(TypedFilterFactory->Config.RegexPattern);

		return true;
	}

	bool FFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		const bool bStartMatch = RegexMatcher.Test(StringBuffer->Read(Edge.Start));
		const bool bEndMatch = RegexMatcher.Test(StringBuffer->Read(Edge.End));
		const bool bResult = TypedFilterFactory->Config.bRequireBothEndpoints ? (bStartMatch && bEndMatch) : (bStartMatch || bEndMatch);
		return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(EdgeEndpointsRegex)

#if WITH_EDITOR
FString UPCGExEdgeEndpointsRegexFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute) + TEXT(" =~ /") + Config.RegexPattern + TEXT("/"),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
