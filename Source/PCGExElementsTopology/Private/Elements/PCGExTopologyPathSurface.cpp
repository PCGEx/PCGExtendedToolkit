// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTopologyPathSurface.h"

#include "PCGComponent.h"
#include "UDynamicMesh.h"
#include "Data/PCGDynamicMeshData.h" // Redundant but required for build on Linux 
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExActorHelpers.h"

#define LOCTEXT_NAMESPACE "TopologyProcessor"
#define PCGEX_NAMESPACE TopologyProcessor

PCGEX_INITIALIZE_ELEMENT(TopologyPathSurface)
PCGEX_ELEMENT_BATCH_POINT_IMPL(TopologyPathSurface)

TArray<FPCGPinProperties> UPCGExTopologyPathSurfaceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::OutputMeshLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

void FPCGExTopologyPathSurfaceContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(TopologyPathSurface)

	FPCGExPathProcessorContext::RegisterAssetDependencies();

	if (Settings->Topology.Material.ToSoftObjectPath().IsValid())
	{
		AddAssetDependency(Settings->Topology.Material.ToSoftObjectPath());
	}
}

bool FPCGExTopologyPathSurfaceElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPathSurface)

	return true;
}

bool FPCGExTopologyPathSurfaceElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyToPathsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPathSurface)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any dataset to generate splines."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExTopologyPathSurface
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = false; //Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

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

		TArray<FVector> ActivePositions;
		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->GetIn(), ActivePositions);

		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTriangulatedPolygon3D(GetInternalMesh(), Settings->Topology.PrimitiveOptions, FTransform::Identity, ActivePositions);

		UVDetails = Settings->Topology.UVChannels;
		UVDetails.Prepare(PointDataFacade);

		/////

		FTransform Transform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		InternalMesh->EditMesh([&](FDynamicMesh3& InMesh)
		{
			const int32 VtxCount = InMesh.MaxVertexID();
			const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();
			const TConstPCGValueRange<FVector4> InColors = PointDataFacade->GetIn()->GetConstColorValueRange();

			InMesh.EnableAttributes();
			InMesh.Attributes()->EnablePrimaryColors();
			InMesh.Attributes()->EnableMaterialID();

			UE::Geometry::FDynamicMeshColorOverlay* Colors = InMesh.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

			TArray<int32> ElemIDs;
			ElemIDs.SetNum(VtxCount);

			for (int i = 0; i < VtxCount; i++)
			{
				InMesh.SetVertex(i, Transform.InverseTransformPosition(InTransforms[i].GetLocation()));
				ElemIDs[i] = Colors->AppendElement(FVector4f(InColors[i]));
			}

			TArray<int32> TriangleIDs;
			TriangleIDs.Reserve(InMesh.TriangleCount());
			for (int32 TriangleID : InMesh.TriangleIndicesItr())
			{
				TriangleIDs.Add(TriangleID);

				const UE::Geometry::FIndex3i Triangle = InMesh.GetTriangle(TriangleID);
				MaterialID->SetValue(TriangleID, 0);
				Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]));
			}

			UVDetails.Write(TriangleIDs, InMesh);
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);


		Settings->Topology.PostProcessMesh(GetInternalMesh());

		////

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
