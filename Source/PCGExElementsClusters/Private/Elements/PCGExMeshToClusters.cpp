// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExMeshToClusters.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/External/PCGExMesh.h"
#include "Data/External/PCGExMeshCommon.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Fitting/PCGExFitting.h"
#include "Fitting/PCGExFittingTasks.h"
#include "GameFramework/Actor.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Math/Geo/PCGExGeo.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE MeshToClusters

#if WITH_EDITOR
void UPCGExMeshToClustersSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		// Rewire Static Mesh
		PCGEX_SHORTHAND_RENAME_PIN(StaticMeshAttribute, StaticMeshConstant, StaticMesh)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExMeshToClustersSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		StaticMesh.Update(StaticMeshInput_DEPRECATED, StaticMeshAttribute_DEPRECATED, StaticMeshConstant_DEPRECATED.ToSoftObjectPath());
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

namespace PCGExMesh
{
	class FGeoStaticMesh;
}

namespace PCGExGeoTask
{
	class FLloydRelax3;
}

namespace PCGExGraphTask
{
	class FCopyGraphToPoint final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FCopyGraphToPoint(const int32 InTaskIndex, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<PCGExGraphs::FGraphBuilder>& InGraphBuilder, const TSharedPtr<PCGExData::FPointIOCollection>& InVtxCollection, const TSharedPtr<PCGExData::FPointIOCollection>& InEdgeCollection, FPCGExTransformDetails* InTransformDetails)
			: FPCGExIndexedTask(InTaskIndex)
			  , PointIO(InPointIO)
			  , GraphBuilder(InGraphBuilder)
			  , VtxCollection(InVtxCollection)
			  , EdgeCollection(InEdgeCollection)
			  , TransformDetails(InTransformDetails)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		TSharedPtr<PCGExData::FPointIOCollection> VtxCollection;
		TSharedPtr<PCGExData::FPointIOCollection> EdgeCollection;

		FPCGExTransformDetails* TransformDetails = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			if (!GraphBuilder || !GraphBuilder->bCompiledSuccessfully)
			{
				return;
			}

			const TSharedPtr<PCGExData::FPointIO> VtxDupe = VtxCollection->Emplace_GetRef(GraphBuilder->NodeDataFacade->GetOut(), PCGExData::EIOInit::Duplicate);
			if (!VtxDupe)
			{
				return;
			}

			VtxDupe->IOIndex = TaskIndex;

			PCGExDataId OutId;
			PCGExClusters::Helpers::SetClusterVtx(VtxDupe, OutId);

			PCGEX_MAKE_SHARED(VtxTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, VtxDupe, TransformDetails);
			Launch(VtxTask);

			for (const TSharedPtr<PCGExData::FPointIO>& Edges : GraphBuilder->EdgesIO->Pairs)
			{
				TSharedPtr<PCGExData::FPointIO> EdgeDupe = EdgeCollection->Emplace_GetRef(Edges->GetOut(), PCGExData::EIOInit::Duplicate);
				if (!EdgeDupe)
				{
					return;
				}

				EdgeDupe->IOIndex = TaskIndex;
				PCGExClusters::Helpers::MarkClusterEdges(EdgeDupe, OutId);

				PCGEX_MAKE_SHARED(EdgeTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, EdgeDupe, TransformDetails);
				Launch(EdgeTask);
			}

			// TODO : Copy & Transform cluster as well for a big perf boost
		}
	};
}


namespace PCGExMeshToCluster
{
	class FExtractMeshAndBuildGraph final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FExtractMeshAndBuildGraph(const int32 InTaskIndex, const TSharedPtr<PCGExMesh::FGeoStaticMesh>& InMesh)
			: FPCGExIndexedTask(InTaskIndex)
			  , Mesh(InMesh)
		{
		}

		TSharedPtr<PCGExMesh::FGeoStaticMesh> Mesh;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			FPCGExMeshToClustersContext* Context = TaskManager->GetContext<FPCGExMeshToClustersContext>();
			PCGEX_SETTINGS(MeshToClusters)

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

			const TSharedPtr<PCGExData::FPointIO> RootVtx = Context->RootVtx->Emplace_GetRef<UPCGExClusterNodesData>();

			if (!RootVtx)
			{
				return;
			}

			RootVtx->IOIndex = TaskIndex;

			UPCGBasePointData* VtxPoints = RootVtx->GetOut();
			PCGEX_MAKE_SHARED(RootVtxFacade, PCGExData::FFacade, RootVtx.ToSharedRef())

			bool bWantsColor = false;
			bool bWantsNormals = false;
			TArray<TSharedPtr<PCGExData::TBuffer<FVector2D>>> UVChannelsWriters;
			TArray<int32> UVChannels;
			TArray<FPCGAttributeIdentifier> UVIdentifiers;

			EPCGPointNativeProperties Allocations = EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed;
			const FStaticMeshVertexBuffers* VertexBuffers = nullptr;

			const FPCGExGeoMeshImportDetails& ImportDetails = Context->ImportDetails;

			if (Context->bWantsImport)
			{
				VertexBuffers = Mesh->RawData.Buffers;

				if (ImportDetails.bImportVertexColor && Mesh->RawData.HasColor())
				{
					Allocations |= EPCGPointNativeProperties::Color;
					bWantsColor = true;
				}

				bWantsNormals = ImportDetails.bImportVertexNormals && Mesh->RawData.HasNormals();

				if (const int32 NumTexCoords = Mesh->RawData.NumTexCoords;
					!ImportDetails.UVChannelIndex.IsEmpty() && NumTexCoords >= 0)
				{
					UVChannels.Reserve(ImportDetails.UVChannelIndex.Num());
					UVChannelsWriters.Reserve(ImportDetails.UVChannelIndex.Num());
					UVIdentifiers.Reserve(ImportDetails.UVChannelIndex.Num());

					for (int i = 0; i < ImportDetails.UVChannelIndex.Num(); i++)
					{
						int32 Channel = ImportDetails.UVChannelIndex[i];
						const FPCGAttributeIdentifier& Id = ImportDetails.UVChannelId[i];

						if (Channel >= NumTexCoords)
						{
							if (ImportDetails.bCreatePlaceholders)
							{
								PCGExData::WriteMark(VtxPoints, Id, ImportDetails.Placeholder);
							}
							continue;
						}

						UVChannels.Add(Channel);
						UVIdentifiers.Add(Id);
					}
				}
			}

			const int32 NumUVChannels = UVChannels.Num();

			const bool bBoundaries = Mesh->DesiredTriangulationType == EPCGExTriangulationType::Boundaries;
			const int32 NumOutPoints = bBoundaries ? Mesh->HullIndices.Num() : Mesh->Vertices.Num();

			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(VtxPoints, NumOutPoints, Allocations);

			// UV channels attributes need to be initialized once we have the final number of points
			for (int32 i = 0; i < NumUVChannels; i++)
			{
				UVChannelsWriters.Add(RootVtxFacade->GetWritable(UVIdentifiers[i], FVector2D::ZeroVector, true, PCGExData::EBufferInit::New));
			}

			TPCGValueRange<FTransform> OutTransforms = VtxPoints->GetTransformValueRange(false);

			// Raw vertex index of each output point. Negative values reference a triangle
			// (centroid points from Dual/Hollow graphs) : -(RawIndex + 1) indexes Mesh->Triangles.
			TConstArrayView<int32> OutRawIndices = Mesh->RawIndices;
			TArray<int32> BoundaryRawIndices;

			if (bBoundaries)
			{
				TMap<int32, int32> IndicesRemap;
				IndicesRemap.Reserve(NumOutPoints);
				BoundaryRawIndices.Reserve(NumOutPoints);

				int32 t = 0;
				for (const int32 i : Mesh->HullIndices)
				{
					IndicesRemap.Add(i, t);
					BoundaryRawIndices.Add(Mesh->RawIndices[i]);
					OutTransforms[t++].SetLocation(Mesh->Vertices[i]);
				}

				OutRawIndices = BoundaryRawIndices;

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
				// Import flags are only set when RawData.bIsValid, which guarantees Buffers
				check(VertexBuffers);

				// After MakeDual, Triangles hold raw vertex indices; after MakeHollowDual they hold dense ones.
				const bool bRawTriangles = Mesh->DesiredTriangulationType == EPCGExTriangulationType::Dual;
				const TArray<int32>& RawIndices = Mesh->RawIndices;
				const TArray<FIntVector3>& Triangles = Mesh->Triangles;

				// Resolves a negative raw index to the raw vertex indices of the triangle it stands for
				auto ResolveTriangle = [&](const int32 InRawIndex)-> FIntVector3
				{
					const FIntVector3& Triangle = Triangles[-(InRawIndex + 1)];
					return bRawTriangles ? Triangle : FIntVector3(RawIndices[Triangle.X], RawIndices[Triangle.Y], RawIndices[Triangle.Z]);
				};

				if (bWantsColor)
				{
					const FColorVertexBuffer& ColorBuffer = VertexBuffers->ColorVertexBuffer;
					TPCGValueRange<FVector4> OutColors = VtxPoints->GetColorValueRange(false);

					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							if (const int32 RawIndex = OutRawIndices[i]; RawIndex >= 0)
							{
								OutColors[i] = FVector4(ColorBuffer.VertexColor(RawIndex));
							}
							else
							{
								const FIntVector3 Triangle = ResolveTriangle(RawIndex);
								OutColors[i] = (FVector4(ColorBuffer.VertexColor(Triangle.X)) + FVector4(ColorBuffer.VertexColor(Triangle.Y)) + FVector4(ColorBuffer.VertexColor(Triangle.Z))) / 3;
							}
						}, 1024);
				}

				for (int32 u = 0; u < NumUVChannels; u++)
				{
					const FStaticMeshVertexBuffer& UVBuffer = VertexBuffers->StaticMeshVertexBuffer;
					const int32 Channel = UVChannels[u];
					PCGExData::TBuffer<FVector2D>* Writer = UVChannelsWriters[u].Get();

					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							if (const int32 RawIndex = OutRawIndices[i]; RawIndex >= 0)
							{
								Writer->SetValue(i, FVector2D(UVBuffer.GetVertexUV(RawIndex, Channel)));
							}
							else
							{
								const FIntVector3 Triangle = ResolveTriangle(RawIndex);
								Writer->SetValue(i, (FVector2D(UVBuffer.GetVertexUV(Triangle.X, Channel)) + FVector2D(UVBuffer.GetVertexUV(Triangle.Y, Channel)) + FVector2D(UVBuffer.GetVertexUV(Triangle.Z, Channel))) / 3);
							}
						}, 1024);
				}

				if (bWantsNormals)
				{
					const FStaticMeshVertexBuffer& TangentsBuffer = *Mesh->RawData.Tangents;

					PCGExMT::ParallelOrSequential(
						NumOutPoints,
						[&](const int32 i)
						{
							FVector4f Normal;
							if (const int32 RawIndex = OutRawIndices[i]; RawIndex >= 0)
							{
								Normal = TangentsBuffer.VertexTangentZ(RawIndex);
							}
							else
							{
								// Summing then normalizing == averaging then normalizing
								const FIntVector3 Triangle = ResolveTriangle(RawIndex);
								Normal = TangentsBuffer.VertexTangentZ(Triangle.X) + TangentsBuffer.VertexTangentZ(Triangle.Y) + TangentsBuffer.VertexTangentZ(Triangle.Z);
							}

							PCGExMesh::AlignTransformUpToNormal(OutTransforms[i], FVector(Normal.X, Normal.Y, Normal.Z));
						}, 1024);
				}
			}

			if (NumOutPoints)
			{
				TPCGValueRange<int32> OutVtxSeeds = VtxPoints->GetSeedValueRange();
				PCGExMT::ParallelOrSequential(
					NumOutPoints,
					[&](const int32 i)
					{
						OutVtxSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[i].GetLocation());
					}, 1024);
			}

			TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(RootVtxFacade.ToSharedRef(), &Context->GraphBuilderDetails);
			GraphBuilder->Graph->InsertEdges(Mesh->Edges, -1);

			Context->GraphBuilders[TaskIndex] = GraphBuilder;

			// We need to write down UVs attributes before compiling the graph
			// as compilation will re-order points and metadata...
			// This is far from ideal but also much less of a headache.
			if (NumUVChannels > 0)
			{
				RootVtxFacade->WriteSynchronous();
			}


			TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
			GraphBuilder->OnCompilationEndCallback = [WeakHandle](const TSharedRef<PCGExGraphs::FGraphBuilder>& InBuilder, const bool bSuccess)
			{
				if (!bSuccess)
				{
					return;
				}
				PCGEX_SHARED_TCONTEXT_VOID(MeshToClusters, WeakHandle)

				SharedContext.Get()->BaseMeshDataCollection->Add(InBuilder->NodeDataFacade->Source);
				SharedContext.Get()->BaseMeshDataCollection->Add(InBuilder->EdgesIO->Pairs);
			};

			GraphBuilder->CompileAsync(Context->GetTaskManager(), true);
		}
	};
}

TArray<FPCGPinProperties> UPCGExMeshToClustersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExMesh::DeclareGeoMeshImportInputs(ImportDetails, PinProperties);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExMeshToClustersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	PCGEX_PIN_POINTS(FName("BaseMeshData"), "Vtx & edges that have been copied to point. Contains one graph per unique mesh asset.", Advanced)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(MeshToClusters)

bool FPCGExMeshToClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(MeshToClusters)
	PCGEX_EXECUTION_CHECK

	if (Context->MainPoints->Pairs.Num() < 1)
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing targets."))
		return false;
	}

	Context->TargetsDataFacade = MakeShared<PCGExData::FFacade>(Context->MainPoints->Pairs[0].ToSharedRef());

	PCGEX_FWD(GraphBuilderDetails)

	PCGEX_FWD(TransformDetails)
	if (!Context->TransformDetails.Init(Context, Context->TargetsDataFacade.ToSharedRef()))
	{
		return false;
	}

	PCGEX_FWD(ImportDetails)
	if (!Context->ImportDetails.Validate(Context))
	{
		return false;
	}
	Context->bWantsImport = Context->ImportDetails.WantsImport();

	if (Settings->StaticMesh.Input == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->StaticMesh.Attribute)
	}

	const TSharedPtr<PCGExData::FPointIO> Targets = Context->MainPoints->Pairs[0];
	Context->MeshIdx.SetNum(Targets->GetNum());

	Context->StaticMeshMap = MakeShared<PCGExMesh::FGeoStaticMeshMap>();
	Context->StaticMeshMap->DesiredTriangulationType = Settings->GraphOutputType;

	// Clusters currently require mesh merging, so the setting is clamped to be
	// no less than a small nonzero value.
	Context->StaticMeshMap->CWTolerance = PCGEx::SafeTolerance(FVector(Settings->VertexMergeHashTolerance));
	Context->StaticMeshMap->bPreciseVertexMerge = Settings->bPreciseVertexMerge;

	Context->RootVtx = MakeShared<PCGExData::FPointIOCollection>(Context); // Make this pinless

	Context->VtxChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxChildCollection->OutputPin = Settings->GetMainOutputPin();

	Context->EdgeChildCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->EdgeChildCollection->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;

	Context->BaseMeshDataCollection = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->BaseMeshDataCollection->OutputPin = FName("BaseMeshData");

	return true;
}

bool FPCGExMeshToClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExMeshToClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(MeshToClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->AdvancePointsIO();
		if (Settings->StaticMesh.Input == EPCGExInputValueType::Constant)
		{
			if (!Settings->StaticMesh.Constant.IsValid())
			{
				return Context->CancelExecution(TEXT("Invalid static mesh constant"));
			}

			const int32 Idx = Context->StaticMeshMap->FindOrAdd(Settings->StaticMesh.Constant);

			if (Idx == -1)
			{
				return Context->CancelExecution(TEXT("Static mesh constant could not be loaded."));
			}

			Context->EDITOR_TrackPath(Settings->StaticMesh.Constant);
			for (int32& Index : Context->MeshIdx)
			{
				Index = Idx;
			}
		}
		else
		{
			FPCGAttributePropertyInputSelector Selector = FPCGAttributePropertyInputSelector();
			Selector.SetAttributeName(Settings->StaticMesh.Attribute);

			const TUniquePtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> PathGetter = MakeUnique<PCGExData::TAttributeBroadcaster<FSoftObjectPath>>();
			if (!PathGetter->Prepare(Selector, Context->MainPoints->Pairs[0].ToSharedRef()))
			{
				return Context->CancelExecution(TEXT("Static mesh attribute does not exists on targets."));
			}

			const UPCGBasePointData* TargetPoints = Context->CurrentIO->GetIn();
			const int32 NumTargets = TargetPoints->GetNumPoints();
			for (int i = 0; i < NumTargets; i++)
			{
				FSoftObjectPath Path = PathGetter->FetchSingle(PCGExData::FConstPoint(TargetPoints, i), FSoftObjectPath());

				if (!Path.IsValid())
				{
					if (!Settings->bIgnoreMeshWarnings)
					{
						PCGE_LOG(Warning, GraphAndLog, FTEXT("Some targets could not have their mesh loaded."));
					}
					Context->MeshIdx[i] = -1;
					continue;
				}

				if (Settings->AttributeHandling == EPCGExMeshAttributeHandling::StaticMeshSoftPath)
				{
					const int32 Idx = Context->StaticMeshMap->FindOrAdd(Path);

					if (Idx == -1)
					{
						if (!Settings->bIgnoreMeshWarnings)
						{
							PCGE_LOG(Warning, GraphAndLog, FTEXT("Some targets could not have their mesh loaded."));
						}
						Context->MeshIdx[i] = -1;
					}
					else
					{
						Context->MeshIdx[i] = Idx;
					}
				}
				else
				{
					TArray<UStaticMeshComponent*> SMComponents;
					if (const AActor* SourceActor = Cast<AActor>(Path.ResolveObject()))
					{
						TArray<UActorComponent*> Components;
						SourceActor->GetComponents(Components);

						for (UActorComponent* Component : Components)
						{
							if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
							{
								SMComponents.Add(StaticMeshComponent);
							}
						}
					}

					if (SMComponents.IsEmpty())
					{
						Context->MeshIdx[i] = -1;
					}
					else
					{
						const int32 Idx = Context->StaticMeshMap->FindOrAdd(TSoftObjectPtr<UStaticMesh>(SMComponents[0]->GetStaticMesh()).ToSoftObjectPath());
						if (Idx == -1)
						{
							if (!Settings->bIgnoreMeshWarnings)
							{
								PCGE_LOG(Warning, GraphAndLog, FTEXT("Some actors have invalid SMCs."));
							}
							Context->MeshIdx[i] = -1;
						}
						else
						{
							Context->MeshIdx[i] = Idx;
						}
					}
				}
			}
		}

		const int32 GSMNums = Context->StaticMeshMap->GSMs.Num();
		Context->GraphBuilders.SetNum(GSMNums);
		for (int i = 0; i < GSMNums; i++)
		{
			Context->GraphBuilders[i] = nullptr;
		}

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		for (int i = 0; i < Context->StaticMeshMap->GSMs.Num(); i++)
		{
			PCGEX_LAUNCH(PCGExMeshToCluster::FExtractMeshAndBuildGraph, i, Context->StaticMeshMap->GSMs[i])
		}

		// Preload all & build local graphs to copy to points later on			
		Context->SetState(PCGExMath::Geo::States::State_ExtractingMesh);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExMath::Geo::States::State_ExtractingMesh)
	{
		Context->SetState(PCGExGraphs::States::State_WritingClusters);

		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();

		const int32 NumTargets = Context->CurrentIO->GetIn()->GetNumPoints();
		for (int i = 0; i < NumTargets; i++)
		{
			const int32 MeshIdx = Context->MeshIdx[i];
			if (MeshIdx == -1)
			{
				continue;
			}
			PCGEX_LAUNCH(PCGExGraphTask::FCopyGraphToPoint, i, Context->CurrentIO, Context->GraphBuilders[MeshIdx], Context->VtxChildCollection, Context->EdgeChildCollection, &Context->TransformDetails)
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExGraphs::States::State_WritingClusters)
	{
		Context->BaseMeshDataCollection->StageOutputs();

		Context->VtxChildCollection->StageOutputs();
		Context->EdgeChildCollection->StageOutputs();

		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
