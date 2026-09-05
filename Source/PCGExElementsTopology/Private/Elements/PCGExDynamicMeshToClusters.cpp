// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDynamicMeshToClusters.h"

#include "PCGExTopology.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExGeoDynMesh.h"
#include "Data/PCGExPointIO.h"
#include "Data/External/PCGExMeshCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Math/Geo/PCGExGeo.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE DynamicMeshToClusters

namespace PCGExDynMeshToCluster
{
	class FExtractDynMeshAndBuildGraph final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FExtractDynMeshAndBuildGraph(
			const int32 InTaskIndex,
			const TSharedPtr<PCGExMesh::FGeoDynMesh>& InMesh)
			: FPCGExIndexedTask(InTaskIndex)
			  , Mesh(InMesh)
		{
		}

		TSharedPtr<PCGExMesh::FGeoDynMesh> Mesh;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			FPCGExDynamicMeshToClustersContext* Context = TaskManager->GetContext<FPCGExDynamicMeshToClustersContext>();
			PCGEX_SETTINGS(DynamicMeshToClusters)

			switch (Mesh->DesiredTriangulationType)
			{
			default: ;
			case EPCGExTriangulationType::Raw:
				Mesh->ExtractMeshSynchronous();
				break;
			case EPCGExTriangulationType::Dual:
				Mesh->TriangulateMeshSynchronous();
				Mesh->MakeDual();
				break;
			case EPCGExTriangulationType::Hollow:
				Mesh->TriangulateMeshSynchronous();
				Mesh->MakeHollowDual();
				break;
			case EPCGExTriangulationType::Boundaries:
				Mesh->TriangulateMeshSynchronous();
				if (Mesh->HullIndices.IsEmpty() || Mesh->HullEdges.IsEmpty())
				{
					return;
				}
				break;
			}

			if (!Mesh->bIsValid || Mesh->Vertices.IsEmpty())
			{
				return;
			}

			// Allocate vertex point data
			const TSharedPtr<PCGExData::FPointIO> RootVtx = Context->VtxCollection->Emplace_GetRef<UPCGExClusterNodesData>();
			if (!RootVtx)
			{
				return;
			}

			RootVtx->IOIndex = TaskIndex;

			UPCGBasePointData* VtxPoints = RootVtx->GetOut();
			PCGEX_MAKE_SHARED(RootVtxFacade, PCGExData::FFacade, RootVtx.ToSharedRef())

			const FPCGExGeoMeshImportDetails& ImportDetails = Context->ImportDetails;

			bool bWantsColor = false;
			bool bWantsNormals = false;
			TArray<FVector4f> AvgColors;
			TArray<FVector3f> AvgNormals;
			TArray<TArray<FVector2f>> AllUVs;
			TArray<TSharedPtr<PCGExData::TBuffer<FVector2D>>> UVChannelsWriters;
			TArray<FPCGAttributeIdentifier> UVIdentifiers;

			EPCGPointNativeProperties Allocations = EPCGPointNativeProperties::Transform;

			if (Context->bWantsImport)
			{
				// Check color availability (computed once, reused in the write passes below)
				if (ImportDetails.bImportVertexColor && Mesh->GetAveragedVertexColors(AvgColors))
				{
					Allocations |= EPCGPointNativeProperties::Color;
					bWantsColor = true;
				}

				// Check normal availability
				bWantsNormals = ImportDetails.bImportVertexNormals && Mesh->GetAveragedVertexNormals(AvgNormals);

				// Check UV availability
				const int32 NumMeshUVChannels = Mesh->GetNumUVChannels();
				if (!ImportDetails.UVChannelIndex.IsEmpty() && NumMeshUVChannels > 0)
				{
					AllUVs.Reserve(ImportDetails.UVChannelIndex.Num());
					UVIdentifiers.Reserve(ImportDetails.UVChannelIndex.Num());

					for (int i = 0; i < ImportDetails.UVChannelIndex.Num(); i++)
					{
						const int32 Channel = ImportDetails.UVChannelIndex[i];
						const FPCGAttributeIdentifier& Id = ImportDetails.UVChannelId[i];

						if (Channel >= NumMeshUVChannels)
						{
							if (ImportDetails.bCreatePlaceholders)
							{
								PCGExData::WriteMark(VtxPoints, Id, ImportDetails.Placeholder);
							}
							continue;
						}

						// An empty array means the channel exists but has no usable overlay data;
						// the attribute is still created and keeps its default value.
						Mesh->GetAveragedVertexUVs(Channel, AllUVs.Emplace_GetRef());
						UVIdentifiers.Add(Id);
					}
				}
			}

			const int32 NumUVChannels = UVIdentifiers.Num();

			const bool bBoundaries = Mesh->DesiredTriangulationType == EPCGExTriangulationType::Boundaries;
			const int32 NumOutPoints = bBoundaries ? Mesh->HullIndices.Num() : Mesh->Vertices.Num();

			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(VtxPoints, NumOutPoints, Allocations);

			// UV channels attributes need to be initialized once we have the final number of points
			UVChannelsWriters.Reserve(NumUVChannels);
			for (int32 i = 0; i < NumUVChannels; i++)
			{
				UVChannelsWriters.Add(RootVtxFacade->GetWritable(UVIdentifiers[i], FVector2D::ZeroVector, true, PCGExData::EBufferInit::New));
			}

			TPCGValueRange<FTransform> OutTransforms = VtxPoints->GetTransformValueRange(false);

			// Import values are indexed by dense vertex; in Boundaries mode output points are a remapped subset.
			TArray<int32> BoundaryToDense;

			if (bBoundaries)
			{
				TMap<int32, int32> IndicesRemap;
				IndicesRemap.Reserve(NumOutPoints);
				BoundaryToDense.Reserve(NumOutPoints);

				int32 t = 0;
				for (const int32 i : Mesh->HullIndices)
				{
					IndicesRemap.Add(i, t);
					BoundaryToDense.Add(i);
					OutTransforms[t++].SetLocation(Mesh->Vertices[i]);
				}

				// Remap hull edges to new dense indices
				Mesh->Edges.Empty();
				for (const uint64 Edge : Mesh->HullEdges)
				{
					uint32 A = 0;
					uint32 B = 0;
					PCGEx::H64(Edge, A, B);
					Mesh->Edges.Add(PCGEx::H64U(IndicesRemap[A], IndicesRemap[B]));
				}
			}
			else
			{
				const TArray<FVector>& MeshVertices = Mesh->Vertices;
				PCGExMT::ParallelOrSequential(
					NumOutPoints,
					[&](const int32 i)
					{
						OutTransforms[i].SetLocation(MeshVertices[i]);
					}, 1024);
			}

			if (bWantsColor || bWantsNormals || NumUVChannels)
			{
				auto ValueIndex = [&](const int32 i) { return bBoundaries ? BoundaryToDense[i] : i; };

				if (bWantsColor)
				{
					TPCGValueRange<FVector4> OutColors = VtxPoints->GetColorValueRange(false);
					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							OutColors[i] = FVector4(AvgColors[ValueIndex(i)]);
						}, 1024);
				}

				for (int32 u = 0; u < NumUVChannels; u++)
				{
					const TArray<FVector2f>& UVs = AllUVs[u];
					if (UVs.IsEmpty())
					{
						continue;
					}

					PCGExData::TBuffer<FVector2D>* Writer = UVChannelsWriters[u].Get();
					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							Writer->SetValue(i, FVector2D(UVs[ValueIndex(i)]));
						}, 1024);
				}

				if (bWantsNormals)
				{
					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							const FVector3f& Normal = AvgNormals[ValueIndex(i)];
							PCGExMesh::AlignTransformUpToNormal(OutTransforms[i], FVector(Normal.X, Normal.Y, Normal.Z));
						}, 1024);
				}
			}

			TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(RootVtxFacade.ToSharedRef(), &Context->GraphBuilderDetails);
			GraphBuilder->Graph->InsertEdges(Mesh->Edges, -1);

			Context->GraphBuilders[TaskIndex] = GraphBuilder;

			// Write UV attributes before compilation (compilation may re-order points)
			if (NumUVChannels > 0)
			{
				RootVtxFacade->WriteSynchronous();
			}

			// Mark vtx/edges on compilation end
			TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
			GraphBuilder->OnCompilationEndCallback = [WeakHandle, TIndex = TaskIndex](const TSharedRef<PCGExGraphs::FGraphBuilder>& InBuilder, const bool bSuccess)
			{
				if (!bSuccess)
				{
					return;
				}
				PCGEX_SHARED_TCONTEXT_VOID(DynamicMeshToClusters, WeakHandle)

				// Mark cluster identification
				PCGExDataId OutId;
				PCGExClusters::Helpers::SetClusterVtx(InBuilder->NodeDataFacade->Source, OutId);
				const int32 EdgeIOIndexBase = TIndex * 100000;
				for (int32 i = 0; i < InBuilder->EdgesIO->Pairs.Num(); i++)
				{
					const TSharedPtr<PCGExData::FPointIO>& SingleEdgeIO = InBuilder->EdgesIO->Pairs[i];
					SingleEdgeIO->IOIndex = EdgeIOIndexBase + i;
					PCGExClusters::Helpers::MarkClusterEdges(SingleEdgeIO, OutId);
				}

				// Add edges to the edge collection for output staging
				SharedContext.Get()->EdgeCollection->Add(InBuilder->EdgesIO->Pairs);
			};

			GraphBuilder->CompileAsync(Context->GetTaskManager(), true);
		}
	};
}

#pragma region UPCGSettings interface

TArray<FPCGPinProperties> UPCGExDynamicMeshToClustersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::SourceMeshLabel, "PCG Dynamic Mesh input(s).", Required)
	PCGExMesh::DeclareGeoMeshImportInputs(ImportDetails, PinProperties);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExDynamicMeshToClustersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputVerticesLabel, "Cluster vertices.", Required)
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Cluster edges.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(DynamicMeshToClusters)

#pragma endregion

bool FPCGExDynamicMeshToClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(DynamicMeshToClusters)
	PCGEX_EXECUTION_CHECK

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGExTopology::Labels::SourceMeshLabel);
	if (Inputs.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing dynamic mesh input(s)."))
		return false;
	}

	// Validate and store input entries
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGDynamicMeshData* DynMeshData = Cast<UPCGDynamicMeshData>(Input.Data);
		if (!DynMeshData || !DynMeshData->GetDynamicMesh())
		{
			if (!Settings->bIgnoreMeshWarnings)
			{
				PCGE_LOG(Warning, GraphAndLog, FTEXT("Some inputs are not valid dynamic mesh data."));
			}
			continue;
		}

		Context->InputEntries.Add(Input);
	}

	if (Context->InputEntries.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No valid dynamic mesh inputs found."));
		return false;
	}

	PCGEX_FWD(GraphBuilderDetails)
	PCGEX_FWD(ImportDetails)
	if (!Context->ImportDetails.Validate(Context))
	{
		return false;
	}
	Context->bWantsImport = Context->ImportDetails.WantsImport();

	// Create output collections
	Context->VtxCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxCollection->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;

	Context->EdgeCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->EdgeCollection->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;

	return true;
}

bool FPCGExDynamicMeshToClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDynamicMeshToClustersElement::AdvanceWork);

	PCGEX_CONTEXT_AND_SETTINGS(DynamicMeshToClusters)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExMath::Geo::States::State_ExtractingMesh);

		PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(Context->GetTaskManager())
		{
			return Context->CancelExecution();
		}

		const int32 NumMeshes = Context->InputEntries.Num();
		Context->GraphBuilders.SetNum(NumMeshes);

		for (int i = 0; i < NumMeshes; i++)
		{
			Context->GraphBuilders[i] = nullptr;
		}

		const FVector SafeTolerance = PCGEx::SafeTolerance(FVector(Settings->VertexMergeHashTolerance));
		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();

		for (int i = 0; i < NumMeshes; i++)
		{
			const UPCGDynamicMeshData* DynMeshData = Cast<UPCGDynamicMeshData>(Context->InputEntries[i].Data);
			const UDynamicMesh* DynMesh = DynMeshData->GetDynamicMesh();

			PCGEX_MAKE_SHARED(GeoMesh, PCGExMesh::FGeoDynMesh, &DynMesh->GetMeshRef(), SafeTolerance, Settings->bPreciseVertexMerge)
			GeoMesh->DesiredTriangulationType = Settings->GraphOutputType;

			PCGEX_LAUNCH(PCGExDynMeshToCluster::FExtractDynMeshAndBuildGraph, i, GeoMesh)
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExMath::Geo::States::State_ExtractingMesh)
	{
		// Forward tags from input entries to output vtx/edge data before staging.
		// Each GraphBuilder at index i corresponds to InputEntries[i].
		// VtxCollection entries are added in task order via Emplace_GetRef with IOIndex set.
		for (const TSharedPtr<PCGExData::FPointIO>& VtxIO : Context->VtxCollection->Pairs)
		{
			const int32 Idx = VtxIO->IOIndex;
			if (Idx >= 0 && Idx < Context->InputEntries.Num())
			{
				VtxIO->Tags->Append(Context->InputEntries[Idx].Tags);
			}
		}

		// Stage outputs (Sort by IOIndex happens inside)
		Context->VtxCollection->StageOutputs();
		Context->EdgeCollection->StageOutputs();

		Context->GraphBuilders.Empty();

		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
