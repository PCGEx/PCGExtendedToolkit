// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeEndpointsCompareNumFilter.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeEndpointsCompareNumFilter"
#define PCGEX_NAMESPACE EdgeEndpointsCompareNumFilter

void UPCGExEdgeEndpointsCompareNumFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<double>(InContext, Config.Attribute);
}

bool UPCGExEdgeEndpointsCompareNumFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.Attribute, Consumable)

	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeEndpointsCompareNumFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeEndpointsCompareNum::FFilter>(this);
}

namespace PCGExEdgeEndpointsCompareNum
{
	bool FFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		NumericBuffer = InPointDataFacade->GetBroadcaster<double>(TypedFilterFactory->Config.Attribute);
		if (!NumericBuffer)
		{
			PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Comparison Attribute, TypedFilterFactory->Config.Attribute)
			return false;
		}

		return true;
	}

	bool FFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		const bool bResult = PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, NumericBuffer->Read(Edge.Start), NumericBuffer->Read(Edge.End), TypedFilterFactory->Config.Tolerance);
		return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(EdgeEndpointsCompareNum)

#if WITH_EDITOR
FString UPCGExEdgeEndpointsCompareNumFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		TEXT("A' ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute) + PCGExCompare::ToString(Config.Comparison) + TEXT(" B' ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
