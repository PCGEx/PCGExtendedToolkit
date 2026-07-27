// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Nodes/PCGExNodeEdgeAngleFilter.h"


#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExNodeEdgeAngleFilter"
#define PCGEX_NAMESPACE NodeEdgeAngleFilter

bool UPCGExNodeEdgeAngleFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}
	Config.DotComparisonDetails.RegisterConsumableAttributesWithData(InContext, InData);
	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNodeEdgeAngleFilterFactory::CreateFilter() const
{
	return MakeShared<FNodeEdgeAngleFilter>(this);
}

bool FNodeEdgeAngleFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!DotComparison.Init(InContext, PointDataFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	bLeavesFallback = TypedFilterFactory->Config.LeavesFallback == EPCGExFilterFallback::Pass;
	bNonBinaryFallback = TypedFilterFactory->Config.LeavesFallback == EPCGExFilterFallback::Pass;

	if (TypedFilterFactory->Config.bInvert)
	{
		bLeavesFallback = !bLeavesFallback;
		bNonBinaryFallback = !bNonBinaryFallback;
	}

	return true;
}

bool FNodeEdgeAngleFilter::Test(const PCGExClusters::FNode& Node) const
{
	if (Node.IsLeaf())
	{
		return bLeavesFallback;
	}
	if (Node.IsComplex())
	{
		return bNonBinaryFallback;
	}

	const bool bPass = DotComparison.Test(FVector::DotProduct(Cluster->GetDir(Node.Index, Node.Links[0].Node), Cluster->GetDir(Node.Index, Node.Links[1].Node)), Node.PointIndex);

	return bPass ? !TypedFilterFactory->Config.bInvert : TypedFilterFactory->Config.bInvert;
}

FNodeEdgeAngleFilter::~FNodeEdgeAngleFilter()
{
	TypedFilterFactory = nullptr;
}


PCGEX_CREATE_FILTER_FACTORY(NodeEdgeAngle)

#if WITH_EDITOR
FString UPCGExNodeEdgeAngleFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(TEXT("Edge Angle"), Config.bInvert);
	// TODO : Proper display name
	//FString DisplayName = TEXT("Edge Angle ") + PCGExCompare::ToString(Config.DotComparisonDetails.Comparison);
	//return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
