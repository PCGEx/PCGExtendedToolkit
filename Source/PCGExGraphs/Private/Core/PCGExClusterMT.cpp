// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClusterMT.h"

#include "Clusters/Artifacts/PCGExCachedFaceEnumerator.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Data/PCGExPointIO.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExClusterData.h"
#include "PCGExHeuristicsHandler.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExClusterFilter.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphHelpers.h"
#include "Core/PCGExPointsMT.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExProjectionDetails.h"

namespace PCGExClusterMT
{
	const FName CachedTangentFramesKey = FName("TangentFrames");

#pragma region Tasks

#define PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP(_NAME, _NUM, _PREPARE, _PROCESS, _COMPLETE, _INLINE) PCGEX_ASYNC_PROCESSOR_LOOP(_NAME, _NUM, _PREPARE, _PROCESS, _COMPLETE, _INLINE, GetClusterBatchChunkSize)

	template <typename T>
	class FStartClusterBatchProcessing final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FStartClusterBatchProcessing)

		FStartClusterBatchProcessing(TSharedPtr<T> InTarget, const bool bScoped)
			: FTask(), Target(InTarget), bScopedIndexLookupBuild(bScoped)
		{
		}

		TSharedPtr<T> Target;
		bool bScopedIndexLookupBuild = false;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			Target->PrepareProcessing(TaskManager, bScopedIndexLookupBuild);
		}
	};

#pragma endregion

	TSharedPtr<PCGExClusters::FCluster> IProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, false, false, false);
	}

	void IProcessor::ForwardCluster() const
	{
		if (UPCGExClusterEdgesData* EdgesData = Cast<UPCGExClusterEdgesData>(EdgeDataFacade->GetOut()))
		{
			EdgesData->SetBoundCluster(Cluster);
		}
	}

	IProcessor::IProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
		: VtxDataFacade(InVtxDataFacade), EdgeDataFacade(InEdgeDataFacade)
	{
	}

	void IProcessor::SetExecutionContext(FPCGExContext* InContext)
	{
		ExecutionContext = InContext;
		WorkHandle = ExecutionContext->GetWorkHandle();
		EdgeDataFacade->bSupportsScopedGet = InContext->bScopedAttributeGet && bAllowEdgesDataFacadeScopedGet;
	}

	void IProcessor::SetProjectionDetails(const FPCGExGeo2DProjectionDetails& InDetails, const TSharedPtr<TArray<FVector2D>>& InProjectedVtxPositions, const bool InWantsProjection)
	{
		ProjectionDetails = InDetails;
		ProjectedVtxPositions = InProjectedVtxPositions;
		bWantsProjection = InWantsProjection;
	}

	void IProcessor::RegisterConsumableAttributesWithFacade() const
	{
		// Gives an opportunity for the processor to register attributes with a valid facade
		// So selectors shortcut can be properly resolved (@Last, etc.)

		if (HeuristicsFactories)
		{
			PCGExFactories::RegisterConsumableAttributesWithFacade(*HeuristicsFactories, VtxDataFacade);
			PCGExFactories::RegisterConsumableAttributesWithFacade(*HeuristicsFactories, EdgeDataFacade);
		}

		if (EdgeFilterFactories)
		{
			PCGExFactories::RegisterConsumableAttributesWithFacade(*EdgeFilterFactories, EdgeDataFacade);
		}
	}

	void IProcessor::SetWantsHeuristics(const bool bRequired, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>* InHeuristicsFactories, const EPCGExHeuristicScoreMode ScoreMode)
	{
		HeuristicScoreMode = ScoreMode;
		HeuristicsFactories = InHeuristicsFactories;
		bWantsHeuristics = bRequired;
	}

	bool IProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TaskManager = InTaskManager;
		PCGEX_ASYNC_CHKD(TaskManager)

		PCGEX_CHECK_WORK_HANDLE(false)

		if (!bBuildCluster) { return true; }

		if (const TSharedPtr<PCGExClusters::FCluster> CachedCluster = PCGExClusters::Helpers::TryGetCachedCluster(VtxDataFacade->Source, EdgeDataFacade->Source))
		{
			Cluster = HandleCachedCluster(CachedCluster.ToSharedRef());
		}

		if (!Cluster)
		{
			Cluster = MakeShared<PCGExClusters::FCluster>(VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup);
			Cluster->bIsOneToOne = bIsOneToOne;

			if (!Cluster->BuildFrom(*EndpointsLookup, ExpectedAdjacency))
			{
				PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("A cluster could not be rebuilt correctly. If you did change the content of vtx/edges collections using non cluster-friendly nodes, make sure to use a 'Sanitize Cluster' to ensure clusters are validated."));
				Cluster.Reset();
				return false;
			}
		}

		if (ProjectedVtxPositions)
		{
			TArray<FVector2D>& ProjectedVtx = *ProjectedVtxPositions.Get();
			Cluster->ProjectedCentroid = FVector2D::ZeroVector;

			if (bWantsProjection && (ProjectionDetails.Method == EPCGExProjectionMethod::BestFit || ProjectionDetails.Method == EPCGExProjectionMethod::LocalTangent))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IProcessor::Process::Project);

				const TConstPCGValueRange<FTransform> InVtxTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();

				const TUniquePtr<PCGExClusters::FCluster::TConstVtxLookup> IdxLookup = MakeUnique<PCGExClusters::FCluster::TConstVtxLookup>(Cluster);
				TArray<int32> PtIndices;
				IdxLookup->Dump(PtIndices);

				// BestFit projection for ProjectedVtxPositions (fallback for non-cell consumers)
				ProjectionDetails.Init(PCGExMath::FBestFitPlane(InVtxTransforms, PtIndices));

				for (const int32 i : PtIndices)
				{
					FVector2D V = FVector2D(ProjectionDetails.ProjectFlat(InVtxTransforms[i].GetLocation()));
					ProjectedVtx[i] = V;
					Cluster->ProjectedCentroid += V;
				}

				// LocalTangent: compute per-node tangent frames and cache on cluster
				if (ProjectionDetails.Method == EPCGExProjectionMethod::LocalTangent)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(IProcessor::Process::TangentFrames);

					const TArray<PCGExClusters::FNode>& NodesRef = *Cluster->Nodes.Get();
					const int32 ClusterNumNodes = NodesRef.Num();

					TSharedPtr<TArray<FQuat>> TangentFrames = MakeShared<TArray<FQuat>>();
					TangentFrames->SetNumUninitialized(ClusterNumNodes);

					// Step 1: Compute raw normal for each node from adjacent edges
					TArray<FVector> NodeNormals;
					NodeNormals.SetNumUninitialized(ClusterNumNodes);

					for (int32 NodeIdx = 0; NodeIdx < ClusterNumNodes; ++NodeIdx)
					{
						const PCGExClusters::FNode& Node = NodesRef[NodeIdx];
						const FVector NodePos = Cluster->GetPos(NodeIdx);
						FVector Normal = FVector::ZeroVector;

						if (Node.Num() >= 2)
						{
							// Find the two most linearly independent edges for a robust cross product
							double BestCross = -1.0;
							FVector BestNormal = FVector::UpVector;

							for (int32 i = 0; i < Node.Num(); ++i)
							{
								const FVector DirA = (Cluster->GetPos(Node.Links[i].Node) - NodePos).GetSafeNormal();
								for (int32 j = i + 1; j < Node.Num(); ++j)
								{
									const FVector DirB = (Cluster->GetPos(Node.Links[j].Node) - NodePos).GetSafeNormal();
									const FVector Cross = FVector::CrossProduct(DirA, DirB);
									const double CrossLen = Cross.Size();
									if (CrossLen > BestCross)
									{
										BestCross = CrossLen;
										BestNormal = Cross / CrossLen;
									}
								}
							}
							Normal = BestNormal;
						}
						else
						{
							Normal = FVector::UpVector;
						}

						NodeNormals[NodeIdx] = Normal;
					}

					// Step 2: BFS normal consistency propagation (flip normals that disagree with parent)
					TArray<bool> Visited;
					Visited.SetNumZeroed(ClusterNumNodes);
					TArray<int32> BFSQueue;
					BFSQueue.Reserve(ClusterNumNodes);
					BFSQueue.Add(0);
					Visited[0] = true;

					for (int32 QueueIdx = 0; QueueIdx < BFSQueue.Num(); ++QueueIdx)
					{
						const int32 CurrentNode = BFSQueue[QueueIdx];
						const PCGExClusters::FNode& Node = NodesRef[CurrentNode];

						for (const PCGExGraphs::FLink& Link : Node.Links)
						{
							const int32 NeighborNode = Link.Node;
							if (Visited[NeighborNode]) { continue; }

							// Flip neighbor normal if it disagrees with current
							if (FVector::DotProduct(NodeNormals[CurrentNode], NodeNormals[NeighborNode]) < 0)
							{
								NodeNormals[NeighborNode] = -NodeNormals[NeighborNode];
							}

							Visited[NeighborNode] = true;
							BFSQueue.Add(NeighborNode);
						}
					}

					// Step 3: Build quaternions from oriented normals
					// Use adaptive X hint per node to avoid degeneracy when normal ≈ hint direction
					for (int32 NodeIdx = 0; NodeIdx < ClusterNumNodes; ++NodeIdx)
					{
						const FVector& N = NodeNormals[NodeIdx];
						const FVector XHint = FMath::Abs(FVector::DotProduct(N, FVector::ForwardVector)) < 0.95
							                      ? FVector::ForwardVector
							                      : FVector::RightVector;
						(*TangentFrames)[NodeIdx] = FRotationMatrix::MakeFromZX(N, XHint).ToQuat();
					}

					// Cache on cluster
					TSharedPtr<PCGExClusters::FCachedTangentFrames> CachedFrames = MakeShared<PCGExClusters::FCachedTangentFrames>();
					CachedFrames->NodeTangentFrames = TangentFrames;
					Cluster->SetCachedData(CachedTangentFramesKey, CachedFrames);
				}
			}
			else
			{
				const TArray<PCGExClusters::FNode>& NodesRef = *Cluster->Nodes.Get();
				for (const PCGExClusters::FNode& Node : NodesRef) { Cluster->ProjectedCentroid += ProjectedVtx[Node.PointIndex]; }
			}

			Cluster->ProjectedCentroid /= Cluster->Nodes->Num();
		}

		NumNodes = Cluster->Nodes->Num();
		NumEdges = Cluster->Edges->Num();

		if (bWantsHeuristics)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FClusterProcessor::Heuristics);
			HeuristicsHandler = PCGExHeuristics::FHandler::CreateHandler(HeuristicScoreMode, ExecutionContext, VtxDataFacade, EdgeDataFacade, *HeuristicsFactories);

			if (!HeuristicsHandler->IsValidHandler()) { return false; }

			HeuristicsHandler->PrepareForCluster(Cluster);
			HeuristicsHandler->CompleteClusterPreparation();
		}

		if (VtxFilterFactories && !InitVtxFilters(VtxFilterFactories)) { return false; }
		if (EdgeFilterFactories && !InitEdgesFilters(EdgeFilterFactories)) { return false; }

		// Building cluster may have taken a while so let's make sure we're still legit
		return TaskManager->IsAvailable();
	}

	void IProcessor::StartParallelLoopForNodes(const int32 PerLoopIterations)
	{
		PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP(Nodes, NumNodes, PrepareLoopScopesForNodes, ProcessNodes, OnNodesProcessingComplete, bForceSingleThreadedProcessNodes)
	}

	void IProcessor::PrepareLoopScopesForNodes(const TArray<PCGExMT::FScope>& Loops)
	{
	}

	void IProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
	}

	void IProcessor::OnNodesProcessingComplete()
	{
	}

	void IProcessor::StartParallelLoopForEdges(const int32 PerLoopIterations)
	{
		PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP(Edges, NumEdges, PrepareLoopScopesForEdges, ProcessEdges, OnEdgesProcessingComplete, bForceSingleThreadedProcessEdges)
	}

	void IProcessor::PrepareLoopScopesForEdges(const TArray<PCGExMT::FScope>& Loops)
	{
	}

	void IProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
	}

	void IProcessor::OnEdgesProcessingComplete()
	{
	}

	void IProcessor::StartParallelLoopForRange(const int32 NumIterations, const int32 PerLoopIterations)
	{
		PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP(Ranges, NumIterations, PrepareLoopScopesForRanges, ProcessRange, OnRangeProcessingComplete, bForceSingleThreadedProcessRange)
	}

	void IProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
	}

	void IProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
	}

	void IProcessor::OnRangeProcessingComplete()
	{
	}

	void IProcessor::CompleteWork()
	{
	}

	void IProcessor::Write()
	{
	}

	void IProcessor::Output()
	{
	}

	void IProcessor::Cleanup()
	{
		HeuristicsHandler.Reset();
		VtxFiltersManager.Reset();
		EdgesFiltersManager.Reset();
		bIsProcessorValid = false;
	}

	bool IProcessor::InitVtxFilters(const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
	{
		if (InFilterFactories->IsEmpty()) { return true; }

		VtxFiltersManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
		VtxFiltersManager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters);
		return VtxFiltersManager->Init(ExecutionContext, *InFilterFactories);
	}

	void IProcessor::FilterVtxScope(const PCGExMT::FScope& Scope, const bool bParallel)
	{
		// Note : Don't forget to prefetch VtxDataFacade buffers
		if (VtxFiltersManager) { VtxFiltersManager->Test(Scope.GetView(*Cluster->Nodes.Get()), VtxFilterCache, bParallel); }
	}

	bool IProcessor::IsNodePassingFilters(const PCGExClusters::FNode& Node) const { return static_cast<bool>(*(VtxFilterCache->GetData() + Node.PointIndex)); }

	bool IProcessor::InitEdgesFilters(const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
	{
		EdgeFilterCache.Init(DefaultEdgeFilterValue, EdgeDataFacade->GetNum());

		if (InFilterFactories->IsEmpty()) { return true; }

		EdgesFiltersManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
		EdgesFiltersManager->bUseEdgeAsPrimary = true;
		EdgesFiltersManager->SetSupportedTypes(&PCGExFactories::ClusterEdgeFilters);
		return EdgesFiltersManager->Init(ExecutionContext, *InFilterFactories);
	}

	void IProcessor::FilterEdgeScope(const PCGExMT::FScope& Scope, const bool bParallel)
	{
		// Note : Don't forget to EdgeDataFacade->FetchScope first

		if (EdgesFiltersManager)
		{
			TArray<PCGExGraphs::FEdge>& EdgesRef = *Cluster->Edges.Get();
			EdgesFiltersManager->Test(Scope.GetView(EdgesRef), Scope.GetView(EdgeFilterCache), bParallel);
		}
	}

	TSharedPtr<IProcessor> IBatch::NewProcessorInstance(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) const
	{
		return nullptr;
	}

	IBatch::IBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: ExecutionContext(InContext), WorkHandle(InContext->GetWorkHandle()), VtxDataFacade(MakeShared<PCGExData::FFacade>(InVtx))
	{
		SetExecutionContext(InContext);
		Edges.Append(InEdges);
	}

	void IBatch::SetExecutionContext(FPCGExContext* InContext)
	{
		ExecutionContext = InContext;
		WorkHandle = ExecutionContext->GetWorkHandle();
	}

	void IBatch::SetProjectionDetails(const FPCGExGeo2DProjectionDetails& InProjectionDetails)
	{
		bWantsProjection = true;
		bWantsPerClusterProjection = InProjectionDetails.Method != EPCGExProjectionMethod::Normal;
		ProjectionDetails = InProjectionDetails;
	}

	void IBatch::PrepareProcessing(const TSharedPtr<PCGExMT::FTaskManager> TaskManagerPtr, const bool bScopedIndexLookupBuild)
	{
		PCGEX_CHECK_WORK_HANDLE_VOID

		TaskManager = TaskManagerPtr;
		VtxDataFacade->bSupportsScopedGet = bAllowVtxDataFacadeScopedGet && ExecutionContext->bScopedAttributeGet;

		const int32 NumVtx = VtxDataFacade->GetNum();

		AllocateVtxPoints();

		if (WantsProjection()) { ProjectionDetails.Init(VtxDataFacade); }

		if (!bScopedIndexLookupBuild || NumVtx < PCGEX_CORE_SETTINGS.SmallClusterSize)
		{
			// Trivial
			PCGExGraphs::Helpers::BuildEndpointsLookup(VtxDataFacade->Source, EndpointsLookup, ExpectedAdjacency);
			if (RequiresGraphBuilder())
			{
				GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(VtxDataFacade, &GraphBuilderDetails);
				GraphBuilder->SourceEdgeFacades = EdgesDataFacades;
			}

			if (WantsProjection() && !WantsPerClusterProjection())
			{
				// Prepare projection early, as we want all points projected from the batch				
				PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManagerPtr, ProjectTask)

				ProjectTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(IBatch::Projection::Complete);
					PCGEX_ASYNC_THIS
					This->OnProcessingPreparationComplete();
				};

				ProjectTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(IBatch::Projection::Range);

					PCGEX_ASYNC_THIS

					const TConstPCGValueRange<FTransform> InVtxTransforms = This->VtxDataFacade->GetIn()->GetConstTransformValueRange();
					const FPCGExGeo2DProjectionDetails& Projection = This->ProjectionDetails;
					TArray<FVector2D>& Proj = *This->ProjectedVtxPositions.Get();

					PCGEX_SCOPE_LOOP(i)
					{
						Proj[i] = FVector2D(Projection.ProjectFlat(InVtxTransforms[i].GetLocation()));
					}
				};

				ProjectTask->StartSubLoops(VtxDataFacade->GetNum(), 4096);
			}
			else
			{
				OnProcessingPreparationComplete();
			}
		}
		else
		{
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManagerPtr, BuildEndpointLookupTask)

			PCGExArrayHelpers::InitArray(ReverseLookup, NumVtx);
			PCGExArrayHelpers::InitArray(ExpectedAdjacency, NumVtx);

			RawLookupAttribute = PCGExMetaHelpers::TryGetConstAttribute<int64>(VtxDataFacade->GetIn(), PCGExClusters::Labels::Attr_PCGExVtxIdx);
			if (!RawLookupAttribute) { return; } // FAIL

			BuildEndpointLookupTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IBatch::BuildLookupTable::Complete);

				PCGEX_ASYNC_THIS

				const int32 Num = This->VtxDataFacade->GetNum();
				This->EndpointsLookup.Reserve(Num);
				for (int i = 0; i < Num; i++) { This->EndpointsLookup.Add(This->ReverseLookup[i], i); }
				This->ReverseLookup.Empty();

				if (This->RequiresGraphBuilder())
				{
					This->GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(This->VtxDataFacade, &This->GraphBuilderDetails);
					This->GraphBuilder->SourceEdgeFacades = This->EdgesDataFacades;
				}

				This->OnProcessingPreparationComplete();
			};

			BuildEndpointLookupTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IBatch::BuildLookupTable::Range);

				PCGEX_ASYNC_THIS

				const TConstPCGValueRange<int64> MetadataEntries = This->VtxDataFacade->GetIn()->GetConstMetadataEntryValueRange();

				PCGEX_SCOPE_LOOP(i)
				{
					uint32 A;
					uint32 B;
					PCGEx::H64(This->RawLookupAttribute->GetValueFromItemKey(MetadataEntries[i]), A, B);

					This->ReverseLookup[i] = A;
					This->ExpectedAdjacency[i] = B;
				}

				if (This->WantsProjection() && !This->WantsPerClusterProjection())
				{
					// Extra loop for projection when desired

					const TConstPCGValueRange<FTransform> InVtxTransforms = This->VtxDataFacade->GetIn()->GetConstTransformValueRange();
					const FPCGExGeo2DProjectionDetails& Projection = This->ProjectionDetails;
					TArray<FVector2D>& Proj = *This->ProjectedVtxPositions.Get();

					PCGEX_SCOPE_LOOP(i)
					{
						Proj[i] = FVector2D(Projection.ProjectFlat(InVtxTransforms[i].GetLocation()));
					}
				}
			};

			BuildEndpointLookupTask->StartSubLoops(VtxDataFacade->GetNum(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		}
	}

	bool IBatch::PrepareSingle(const TSharedPtr<IProcessor>& InProcessor)
	{
		return true;
	}

	void IBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		if (VtxFilterFactories)
		{
			PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, *VtxFilterFactories, FacadePreloader);
		}

		// TODO : Preload heuristics that depends on Vtx metadata
	}

	void IBatch::OnProcessingPreparationComplete()
	{
		PCGEX_CHECK_WORK_HANDLE_OR_VOID(!bIsBatchValid)

		VtxFacadePreloader = MakeShared<PCGExData::FFacadePreloader>(VtxDataFacade);
		RegisterBuffersDependencies(*VtxFacadePreloader);

		VtxFacadePreloader->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]
		{
			PCGEX_ASYNC_THIS
			This->Process();
		};

		VtxFacadePreloader->StartLoading(TaskManager);
	}

	void IBatch::Process()
	{
		bIsBatchValid = false;

		PCGEX_ASYNC_CHKD_VOID(TaskManager)

		if (VtxDataFacade->GetNum() <= 1) { return; }
		if (VtxFilterFactories)
		{
			VtxFilterCache = MakeShared<TArray<int8>>();
			VtxFilterCache->Init(DefaultVtxFilterValue, VtxDataFacade->GetNum());

			PCGExFactories::RegisterConsumableAttributesWithFacade(*VtxFilterFactories, VtxDataFacade);
		}

		bIsBatchValid = true;

		for (const TSharedPtr<PCGExData::FPointIO>& IO : Edges)
		{
			const TSharedPtr<IProcessor> NewProcessor = NewProcessorInstance(VtxDataFacade, (*EdgesDataFacades)[IO->IOIndex]);

			NewProcessor->SetExecutionContext(ExecutionContext);

			NewProcessor->ParentBatch = SharedThis(this);
			NewProcessor->VtxFilterFactories = VtxFilterFactories;
			NewProcessor->EdgeFilterFactories = EdgeFilterFactories;
			NewProcessor->VtxFilterCache = VtxFilterCache;

			NewProcessor->NodeIndexLookup = NodeIndexLookup;
			NewProcessor->EndpointsLookup = &EndpointsLookup;
			NewProcessor->ExpectedAdjacency = &ExpectedAdjacency;
			NewProcessor->BatchIndex = Processors.Num();

			if (WantsProjection()) { NewProcessor->SetProjectionDetails(ProjectionDetails, ProjectedVtxPositions, WantsPerClusterProjection()); }

			if (RequiresGraphBuilder()) { NewProcessor->GraphBuilder = GraphBuilder; }

			NewProcessor->SetWantsHeuristics(WantsHeuristics(), HeuristicsFactories, HeuristicsScoreMode);

			NewProcessor->RegisterConsumableAttributesWithFacade();

			if (!PrepareSingle(NewProcessor)) { continue; }

			Processors.Add(NewProcessor.ToSharedRef());

			NewProcessor->bIsTrivial = IO->GetNum() < PCGEX_CORE_SETTINGS.SmallClusterSize;
		}

		StartProcessing();
	}

	void IBatch::StartProcessing()
	{
		if (!bIsBatchValid) { return; }

		PCGEX_ASYNC_MT_LOOP_TPL(
			Process, bForceSingleThreadedProcessing, {Processor->bIsProcessorValid = Processor->Process(This->TaskManager); }, {
			Process->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]() { PCGEX_ASYNC_THIS This->OnInitialPostProcess(); }; })
	}

	void IBatch::OnInitialPostProcess()
	{
	}

	int32 IBatch::GatherValidClusters()
	{
		ValidClusters.Empty();

		for (const TSharedRef<IProcessor>& P : Processors)
		{
			if (!P->Cluster) { continue; }
			ValidClusters.Add(P->Cluster);
		}
		return ValidClusters.Num();
	}

	void IBatch::CompleteWork()
	{
		if (bSkipCompletion) { return; }
		if (!bIsBatchValid) { return; }

		PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(CompleteWork, bForceSingleThreadedCompletion, {Processor->CompleteWork(); }, {})
	}

	void IBatch::Write()
	{
		PCGEX_CHECK_WORK_HANDLE_VOID

		if (!bIsBatchValid) { return; }

		PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(Write, bForceSingleThreadedWrite, {Processor->Write(); }, {})

		if (bWriteVtxDataFacade && bIsBatchValid) { VtxDataFacade->WriteFastest(TaskManager); }
	}

	const PCGExGraphs::FGraphMetadataDetails* IBatch::GetGraphMetadataDetails()
	{
		return nullptr;
	}

	void IBatch::CompileGraphBuilder(const bool bOutputToContext)
	{
		PCGEX_CHECK_WORK_HANDLE_OR_VOID(!GraphBuilder || !bIsBatchValid)

		if (bOutputToContext)
		{
			GraphBuilder->OnCompilationEndCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TSharedRef<PCGExGraphs::FGraphBuilder>& InBuilder, const bool bSuccess)
			{
				PCGEX_ASYNC_THIS

				if (!bSuccess)
				{
					// TODO : Log error
					return;
				}

				if (TSharedPtr<PCGExData::FPointIOCollection> OutCollection = This->GraphEdgeOutputCollection.Pin())
				{
					InBuilder->MoveEdgesOutputs(OutCollection, This->VtxDataFacade->Source->IOIndex * 100000);
				}
				else { InBuilder->StageEdgesOutputs(); }
			};
		}

		GraphBuilder->CompileAsync(TaskManager, true, GetGraphMetadataDetails());
	}

	void IBatch::Output()
	{
		if (!bIsBatchValid) { return; }
		for (const TSharedRef<IProcessor>& P : Processors)
		{
			if (!P->bIsProcessorValid) { continue; }
			P->Output();
		}
	}

	void IBatch::Cleanup()
	{
		for (const TSharedRef<IProcessor>& P : Processors) { P->Cleanup(); }
		Processors.Empty();
	}

	void IBatch::AllocateVtxPoints()
	{
		NodeIndexLookup = MakeShared<PCGEx::FIndexLookup>(VtxDataFacade->GetNum());

		if (WantsProjection())
		{
			ProjectedVtxPositions = MakeShared<TArray<FVector2D>>();
			ProjectedVtxPositions->SetNumUninitialized(VtxDataFacade->GetNum());
		}

		if (AllocateVtxProperties == EPCGPointNativeProperties::None) { return; }
		if (VtxDataFacade->GetOut() && VtxDataFacade->GetIn() != VtxDataFacade->GetOut())
		{
			VtxDataFacade->GetOut()->AllocateProperties(AllocateVtxProperties);
		}
	}

	void ScheduleBatch(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<IBatch>& Batch, const bool bScopedIndexLookupBuild)
	{
		PCGEX_LAUNCH(FStartClusterBatchProcessing<IBatch>, Batch, bScopedIndexLookupBuild)
	}

	void CompleteBatches(const TArrayView<TSharedPtr<IBatch>> Batches)
	{
		for (const TSharedPtr<IBatch>& Batch : Batches) { Batch->CompleteWork(); }
	}

#undef PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP
}
