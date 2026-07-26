// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeEndpointsCompareStrFilter.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeEndpointsCompareStrFilter"
#define PCGEX_NAMESPACE EdgeEndpointsCompareStrFilter

void UPCGExEdgeEndpointsCompareStrFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<FString>(InContext, Config.Attribute);
}

bool UPCGExEdgeEndpointsCompareStrFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.Attribute, Consumable)

	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeEndpointsCompareStrFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeEndpointsCompareStr::FFilter>(this);
}

namespace PCGExEdgeEndpointsCompareStr
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

		return true;
	}

	bool FFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		const bool bResult = PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, StringBuffer->Read(Edge.Start), StringBuffer->Read(Edge.End));
		return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(EdgeEndpointsCompareStr)

#if WITH_EDITOR
FString UPCGExEdgeEndpointsCompareStrFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute) + PCGExCompare::ToString(Config.Comparison),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
