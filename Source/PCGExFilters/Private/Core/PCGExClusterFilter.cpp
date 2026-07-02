// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClusterFilter.h"

#include "Core/PCGExFilterTypeSets.h"

#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoFilterCluster, UPCGExClusterFilterFactoryData)
PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoFilterVtx, UPCGExNodeFilterFactoryData)

FName UPCGExVtxFilterProviderSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputFilterLabelNode;
}

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoFilterEdge, UPCGExEdgeFilterFactoryData)

FName UPCGExEdgeFilterProviderSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputFilterLabelEdge;
}

namespace PCGExClusterFilter
{
	PCGExFilters::EType IFilter::GetFilterType() const
	{
		return PCGExFilters::EType::Node;
	}

	// Guard: cluster filters must be initialized through the cluster Init() path first.
	// If called directly (e.g. from a non-cluster context), this fails with an error.
	bool IFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		if (!bInitForCluster)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Using a Cluster filter without cluster data"));
			return false;
		}
		return PCGExPointFilter::IFilter::Init(InContext, InPointDataFacade);
	}

	bool IFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		bInitForCluster = true;
		Cluster = InCluster;
		EdgeDataFacade = InEdgeDataFacade;
		if (!PCGExPointFilter::IFilter::Init(InContext, InPointDataFacade))
		{
			return false;
		}
		return true;
	}

	// Sizes the Results cache based on the filter type: node filters cache per-node,
	// edge filters cache per-edge.
	void IFilter::PostInit()
	{
		if (!bCacheResults)
		{
			return;
		}
		const int32 NumResults = GetFilterType() == PCGExFilters::EType::Node ? Cluster->Nodes->Num() : EdgeDataFacade->Source->GetNum();
		Results.Init(false, NumResults);
	}

	bool IVtxFilter::Test(const int32 Index) const
	{
		return IFilter::Test(*Cluster->GetNode(Index));
	}

	bool IVtxFilter::Test(const PCGExClusters::FNode& Node) const
	{
		return IFilter::Test(Node);
	}

	bool IVtxFilter::Test(const PCGExGraphs::FEdge& Edge) const PCGEX_NOT_IMPLEMENTED_RET(TVtxFilter::Test(const PCGExGraphs::FIndexedEdge& Edge), false)

	bool IEdgeFilter::Test(const int32 Index) const
	{
		return IFilter::Test(*Cluster->GetEdge(Index));
	}

	bool IEdgeFilter::Test(const PCGExClusters::FNode& Node) const PCGEX_NOT_IMPLEMENTED_RET(TEdgeFilter::Test(const PCGExClusters::FNode& Node), false)

	bool IEdgeFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		return IFilter::Test(Edge);
	}

	FManager::FManager(const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
		: PCGExPointFilter::FManager(InPointDataFacade)
		  , Cluster(InCluster)
		  , EdgeDataFacade(InEdgeDataFacade)
	{
	}

	// Routes filter initialization based on factory type: cluster-aware filters go through
	// the cluster Init() path with full topology access, while regular point filters use
	// the standard Init(FFacade). When bUseEdgeAsPrimary is set, non-cluster filters
	// receive edge data instead of vertex data.
	bool FManager::InitFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& Filter)
	{
		if (PCGExFactories::SupportsClusterFilters().Contains(Filter->Factory->GetDataTypeId()))
		{
			const TSharedPtr<IFilter> ClusterFilter = StaticCastSharedPtr<IFilter>(Filter);
			return ClusterFilter->Init(InContext, Cluster, PointDataFacade, EdgeDataFacade);
		}

		return Filter->Init(InContext, bUseEdgeAsPrimary ? EdgeDataFacade : PointDataFacade);
	}

	void FManager::InitCache()
	{
		const int32 NumResults = Cluster->Nodes->Num();
		Results.Init(false, NumResults);
	}
}
