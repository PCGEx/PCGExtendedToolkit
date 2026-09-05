// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTopologyClusterSurface.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Data/PCGExData.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/PolygonFunctions.h"

#define LOCTEXT_NAMESPACE "TopologyClustersProcessor"
#define PCGEX_NAMESPACE TopologyClustersProcessor

PCGEX_INITIALIZE_ELEMENT(TopologyClusterSurface)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(TopologyClusterSurface)

bool FPCGExTopologyClusterSurfaceElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExTopologyClustersProcessorElement::Boot(InContext))
	{
		return false;
	}
	PCGEX_CONTEXT_AND_SETTINGS(TopologyClusterSurface)

	return true;
}

bool FPCGExTopologyClusterSurfaceElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTopologyClustersProcessorElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TopologyClusterSurface)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->SetProjectionDetails(Settings->ProjectionDetails);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputBatches();

	return Context->TryComplete();
}


namespace PCGExTopologyClusterSurface
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyClusterSurface::Process);

		if (!TProcessor<FPCGExTopologyClusterSurfaceContext, UPCGExTopologyClusterSurfaceSettings>::Process(InTaskManager))
		{
			return false;
		}

		// Build or get the shared enumerator from constraints (enables reuse)
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		// Enumerate all cells - wrapper detected by winding and stored in constraints if bOmitWrappingBounds
		Enumerator->EnumerateAllFaces(ValidCells, CellsConstraints.ToSharedRef(), nullptr, Settings->Constraints.bOmitWrappingBounds);

		return true;
	}

	void FProcessor::CompleteWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyClusterSurface::CompleteWork);

		// Build polygons from enumerated cells
		FGeometryScriptGeneralPolygonList ClusterPolygonList;
		ClusterPolygonList.Reset();

		TArray<FGeometryScriptSimplePolygon> Polygons;
		Polygons.Reserve(ValidCells.Num() + 1);

		for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
		{
			if (!Cell || Cell->Polygon.IsEmpty())
			{
				continue;
			}

			FGeometryScriptSimplePolygon& Polygon = Polygons.Emplace_GetRef();
			Polygon.Reset(Cell->Polygon.Num());
			Polygon.Vertices->Append(Cell->Polygon);
		}

		if (Polygons.IsEmpty() && CellsConstraints->WrapperCell && Settings->Constraints.bKeepWrapperIfSolePath)
		{
			FGeometryScriptSimplePolygon& Polygon = Polygons.Emplace_GetRef();
			Polygon.Reset(CellsConstraints->WrapperCell->Polygon.Num());
			Polygon.Vertices->Append(CellsConstraints->WrapperCell->Polygon);
		}

		if (Polygons.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		UGeometryScriptLibrary_PolygonListFunctions::AppendPolygonList(
			ClusterPolygonList,
			UGeometryScriptLibrary_PolygonListFunctions::CreatePolygonListFromSimplePolygons(Polygons));

		bool bTriangulationError = false;

		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendPolygonListTriangulation(
			GetInternalMesh(),
			Settings->Topology.PrimitiveOptions,
			FTransform::Identity,
			ClusterPolygonList,
			Settings->Topology.TriangulationOptions,
			bTriangulationError);

		if (bTriangulationError && !Settings->Topology.bQuietTriangulationError)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Triangulation error."));
		}

		ApplyPointData();
	}

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
