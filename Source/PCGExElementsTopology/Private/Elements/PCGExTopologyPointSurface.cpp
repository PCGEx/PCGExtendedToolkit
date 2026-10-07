// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements//PCGExTopologyPointSurface.h"

#include "PCGComponent.h"
#include "UDynamicMesh.h"
#include "Async/ParallelFor.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGDynamicMeshData.h" // Redundant but required for build on Linux 
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExActorHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExProjectionDetails.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE TopologyPointSurface

namespace PCGExGeoTask
{
	class FLloydRelax2;
}

TArray<FPCGPinProperties> UPCGExTopologyPointSurfaceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::OutputMeshLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(TopologyPointSurface)

void FPCGExTopologyPointSurfaceContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(TopologyPointSurface)

	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	if (Settings->Topology.Material.ToSoftObjectPath().IsValid())
	{
		AddAssetDependency(Settings->Topology.Material.ToSoftObjectPath());
	}
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(TopologyPointSurface)

bool FPCGExTopologyPointSurfaceElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPointSurface)

	return true;
}

bool FPCGExTopologyPointSurfaceElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTopologyPointSurfaceElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPointSurface)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 3 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 3)
				{
					bHasInvalidInputs = true;
					return false;
				}

				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid inputs to build from."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExTopologyPointSurface
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPointSurface::Process);

		PointDataFacade->bSupportsScopedGet = false;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Prep data

		bIsPreviewMode = PCGExHelpers::IsSourceInPreviewMode(ExecutionContext);

		InternalMeshData = Context->ManagedObjects->New<UPCGDynamicMeshData>();
		if (!InternalMeshData)
		{
			return false;
		}

		InternalMesh = Context->ManagedObjects->New<UDynamicMesh>();
		InternalMesh->InitializeMesh();

		if (InternalMeshData)
		{
			InternalMeshData->Initialize(InternalMesh, true);
			InternalMesh = InternalMeshData->GetMutableDynamicMesh();
			if (UMaterialInterface* Material = Settings->Topology.Material.Get())
			{
				InternalMeshData->SetMaterials({Material});
			}
		}

		// Project points

		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(PointDataFacade))
		{
			return false;
		}

		// Build delaunay

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

		TArray<FVector2D> VertexPositions;
		VertexPositions.Init(FVector2D::ZeroVector, InTransforms.Num());
		ProjectionDetails.Project(InTransforms, VertexPositions);

		TArray<FIntPoint> ConstrainedEdges;
		TArray<int32> PositionsToVertexIDs;
		bool bHasBadVertices = false;

		FGeometryScriptConstrainedDelaunayTriangulationOptions TriangulationOptions{};
		TriangulationOptions.bRemoveDuplicateVertices = true;

		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDelaunayTriangulation2D(InternalMesh, Settings->Topology.PrimitiveOptions, FTransform::Identity, VertexPositions, ConstrainedEdges, TriangulationOptions, PositionsToVertexIDs, bHasBadVertices, nullptr);

		if (PositionsToVertexIDs.IsEmpty())
		{
			return false;
		}

		UVDetails = Settings->Topology.UVChannels;
		UVDetails.Prepare(PointDataFacade);

		FTransform Transform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPointSurface::EditMesh)

			int8 bHasInvalidVertices = false;

			InternalMesh->EditMesh([&](FDynamicMesh3& InMesh)
			{
				FVector4f DefaultVertexColor = FVector4f(Settings->Topology.DefaultVertexColor);

				const int32 VtxCount = InMesh.MaxVertexID();
				const TConstPCGValueRange<FVector4> InColors = PointDataFacade->GetIn()->GetConstColorValueRange();

				InMesh.EnableAttributes();
				InMesh.Attributes()->EnablePrimaryColors();
				InMesh.Attributes()->EnableMaterialID();

				UE::Geometry::FDynamicMeshColorOverlay* Colors = InMesh.Attributes()->PrimaryColors();
				UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

				TArray<int32> ElemIDs;
				ElemIDs.SetNum(VtxCount);

				for (int32 i = 0; i < VtxCount; i++)
				{
					ElemIDs[i] = Colors->AppendElement(DefaultVertexColor);
				}

				ParallelFor(VtxCount, [&](int32 i)
				{
					const int32 VtxID = PositionsToVertexIDs[i];

					if (VtxID == -1)
					{
						FPlatformAtomics::InterlockedExchange(&bHasInvalidVertices, 1);
						return;
					}

					InMesh.SetVertex(VtxID, Transform.InverseTransformPosition(InTransforms[i].GetLocation()));
					Colors->SetElement(ElemIDs[i], FVector4f(InColors[i]));
				});

				TArray<int32> TriangleIDs;
				TriangleIDs.Reserve(InMesh.TriangleCount());
				for (int32 TriangleID : InMesh.TriangleIndicesItr())
				{
					TriangleIDs.Add(TriangleID);

					const UE::Geometry::FIndex3i Triangle = InMesh.GetTriangle(TriangleID);
					MaterialID->SetValue(TriangleID, 0);
					Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]));
				}

				UVDetails.Write(TriangleIDs, PositionsToVertexIDs, InMesh);
			}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);
		}

		if (bHasBadVertices && !Settings->bQuietBadVerticesWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Some inputs have bad vertices : some points will be skipped (most likely collocated points)"));
		}

		if (Settings->bAttemptRepair)
		{
			UGeometryScriptLibrary_MeshRepairFunctions::RepairMeshDegenerateGeometry(InternalMesh, Settings->RepairDegenerate);
		}

		Settings->Topology.PostProcessMesh(InternalMesh);

		return true;
	}

	void FProcessor::Output()
	{
		if (!this->bIsProcessorValid)
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPathSplineMesh::FProcessor::Output);

		if (InternalMeshData)
		{
			Context->StageOutput(
				InternalMeshData, PCGExTopology::MeshOutputLabel, PCGExData::EStaging::Managed,
				PointDataFacade->Source->Tags->Flatten());
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
