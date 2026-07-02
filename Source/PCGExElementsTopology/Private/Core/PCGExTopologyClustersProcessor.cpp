// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExTopologyClustersProcessor.h"

#include "CoreMinimal.h"
#include "PCGComponent.h"
#include "PCGExLog.h"
#include "UDynamicMesh.h"
#include "Async/ParallelFor.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "TopologyProcessor"
#define PCGEX_NAMESPACE TopologyProcessor

PCGExData::EIOInit UPCGExTopologyClustersProcessorSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExTopologyClustersProcessorSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

TArray<FPCGPinProperties> UPCGExTopologyClustersProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExClusters::Labels::SourceHolesLabel, "Omit cells that contain any points from this dataset", Normal)
	if (SupportsEdgeConstraints())
	{
		PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeConstrainsFiltersLabel, "Constrained edges filters.", Normal)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExTopologyClustersProcessorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::OutputMeshLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

void FPCGExTopologyClustersProcessorContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(TopologyClustersProcessor)

	FPCGExClustersProcessorContext::RegisterAssetDependencies();

	if (Settings->Topology.Material.ToSoftObjectPath().IsValid())
	{
		AddAssetDependency(Settings->Topology.Material.ToSoftObjectPath());
	}
}

bool FPCGExTopologyClustersProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(TopologyClustersProcessor)

	Context->HolesFacade = PCGExData::TryGetSingleFacade(Context, PCGExClusters::Labels::SourceHolesLabel, false, false);
	if (Context->HolesFacade && Settings->ProjectionDetails.Method == EPCGExProjectionMethod::Normal)
	{
		Context->Holes = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), Settings->ProjectionDetails);
		Context->Holes->EnsureProjected(); // Project once upfront
	}

	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceEdgeConstrainsFiltersLabel, Context->EdgeConstraintsFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);

	Context->HashMaps.Init(nullptr, Context->MainPoints->Num());
	return true;
}

namespace PCGExTopologyEdges
{
	IProcessor::IProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
		: PCGExClusterMT::IProcessor(InVtxDataFacade, InEdgeDataFacade)
	{
		DefaultEdgeFilterValue = false;
	}

	void IProcessor::InitConstraints()
	{
	}

	TSharedPtr<PCGExClusters::FCluster> IProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		// Create a light working copy with nodes only, will be deleted.
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, true, false, false);
	}

	bool IProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		FPCGExTopologyClustersProcessorContext* Context = static_cast<FPCGExTopologyClustersProcessorContext*>(ExecutionContext);
		const UPCGExTopologyClustersProcessorSettings* Settings = ExecutionContext->GetInputSettings<UPCGExTopologyClustersProcessorSettings>();

		EdgeDataFacade->bSupportsScopedGet = true;
		EdgeFilterFactories = &Context->EdgeConstraintsFilterFactories;

		ProjectedHashMap = Context->HashMaps[VtxDataFacade->Source->IOIndex];

		if (!PCGExClusterMT::IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Context->HolesFacade)
		{
			Holes = Context->Holes ? Context->Holes : MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), this->ProjectionDetails);
			if (Holes)
			{
				Holes->EnsureProjected();
			} // Project once upfront if not already done
		}

		UVDetails = Settings->Topology.UVChannels;
		UVDetails.Prepare(VtxDataFacade);

		bIsPreviewMode = ExecutionContext->GetComponent()->IsInPreviewMode();

		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->Reserve(Cluster->Edges->Num());

		if (Settings->Constraints.bOmitWrappingBounds)
		{
			CellsConstraints->BuildWrapperCell(Cluster.ToSharedRef(), this->ProjectionDetails);
		}
		CellsConstraints->Holes = Holes;

		InitConstraints();

		for (PCGExClusters::FNode& Node : *Cluster->Nodes)
		{
			Node.bValid = false;
		} // Invalidate all edges, triangulation will mark valid nodes to rebuild an index

		// IMPORTANT : Need to wait for projection to be completed.
		// Children should start work only in CompleteWork!!

		InternalMeshData = Context->ManagedObjects->New<UPCGDynamicMeshData>();
		if (!InternalMeshData)
		{
			return false;
		}

		InternalMesh = Context->ManagedObjects->New<UDynamicMesh>();
		InternalMesh->InitializeMesh();

		InternalMeshData->Initialize(InternalMesh, true);
		InternalMesh = InternalMeshData->GetMutableDynamicMesh();
		if (UMaterialInterface* Material = Settings->Topology.Material.Get())
		{
			InternalMeshData->SetMaterials({Material});
		}

		return true;
	}

	void IProcessor::Output()
	{
		if (!this->bIsProcessorValid)
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPathSplineMesh::FProcessor::Output);

		FPCGExTopologyClustersProcessorContext* Context = static_cast<FPCGExTopologyClustersProcessorContext*>(ExecutionContext);

		if (InternalMeshData)
		{
			TSet<FString> MeshTags;

			EdgeDataFacade->Source->Tags->DumpTo(MeshTags);
			VtxDataFacade->Source->Tags->DumpTo(MeshTags);

			Context->StageOutput(InternalMeshData, PCGExTopology::MeshOutputLabel, PCGExData::EStaging::Managed, MeshTags);
		}
	}

	void IProcessor::Cleanup()
	{
		PCGExClusterMT::IProcessor::Cleanup();
		CellsConstraints->Cleanup();
	}

	void IProcessor::FilterConstrainedEdgeScope(const PCGExMT::FScope& Scope)
	{
		int32 LocalConstrainedEdgesNum = 0;
		PCGEX_SCOPE_LOOP(i)
		{
			if (EdgeFilterCache[i])
			{
				LocalConstrainedEdgesNum++;
			}
		}
		FPlatformAtomics::InterlockedAdd(&ConstrainedEdgesNum, LocalConstrainedEdgesNum);
	}

	void IProcessor::ApplyPointData()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TopologyClustersProcessor::ApplyPointData);

		FPCGExTopologyClustersProcessorContext* Context = static_cast<FPCGExTopologyClustersProcessorContext*>(ExecutionContext);
		const UPCGExTopologyClustersProcessorSettings* Settings = ExecutionContext->GetInputSettings<UPCGExTopologyClustersProcessorSettings>();

		FTransform Transform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		InternalMesh->EditMesh([&](FDynamicMesh3& InMesh)
		{
			const int32 VtxCount = InMesh.MaxVertexID();
			const TConstPCGValueRange<FTransform> InTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();
			const TConstPCGValueRange<FVector4> InColors = VtxDataFacade->GetIn()->GetConstColorValueRange();
			const TMap<uint64, int32>& HashMapRef = *ProjectedHashMap;

			FVector4f DefaultVertexColor = FVector4f(Settings->Topology.DefaultVertexColor);

			InMesh.EnableAttributes();
			InMesh.Attributes()->EnablePrimaryColors();
			InMesh.Attributes()->EnableMaterialID();

			UE::Geometry::FDynamicMeshColorOverlay* Colors = InMesh.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

			TArray<int32> VtxIDs;
			VtxIDs.Init(-1, VtxCount);

			TArray<int32> ElemIDs;
			ElemIDs.SetNum(VtxCount);

			for (int32 i = 0; i < VtxCount; i++)
			{
				ElemIDs[i] = Colors->AppendElement(DefaultVertexColor);
			}

			ParallelFor(VtxCount, [&](int32 i)
			{
				const int32* WP = HashMapRef.Find(PCGEx::GH2(InMesh.GetVertex(i), CWTolerance));
				if (WP)
				{
					const int32 PointIndex = *WP;
					VtxIDs[i] = PointIndex;
					InMesh.SetVertex(i, Transform.InverseTransformPosition(InTransforms[PointIndex].GetLocation()));
					Colors->SetElement(ElemIDs[i], FVector4f(InColors[PointIndex]));
				}
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

			UVDetails.Write(TriangleIDs, VtxIDs, InMesh);
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);

		Settings->Topology.PostProcessMesh(GetInternalMesh());
	}

	IBatch::IBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: PCGExClusterMT::IBatch(InContext, InVtx, InEdges)
	{
		ProjectedHashMap = MakeShared<TMap<uint64, int32>>();
		ProjectedHashMap->Reserve(InVtx->GetNum());
		static_cast<FPCGExTopologyClustersProcessorContext*>(InContext)->HashMaps[InVtx->IOIndex] = ProjectedHashMap;
	}

	void IBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		PCGExClusterMT::IBatch::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(TopologyClustersProcessor)

		check(Settings);

		Settings->Topology.UVChannels.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);

		if (Settings->SupportsEdgeConstraints())
		{
			PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->EdgeConstraintsFilterFactories, FacadePreloader);
		}
	}

	void IBatch::Output()
	{
		if (!this->bIsBatchValid)
		{
			return;
		}

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(TopologyClustersProcessor)
		PCGExClusterMT::IBatch::Output();
	}

	void IBatch::OnInitialPostProcess()
	{
		const int32 NumVtx = VtxDataFacade->GetNum();

		TMap<uint64, int32>& MP = *ProjectedHashMap;
		const TArray<FVector2D>& PP = *this->ProjectedVtxPositions.Get();

		for (int i = 0; i < NumVtx; i++)
		{
			MP.Add(PCGEx::GH2(PP[i], CWTolerance), i);
		}

		PCGExClusterMT::IBatch::OnInitialPostProcess();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
