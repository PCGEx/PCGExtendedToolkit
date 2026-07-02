// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExConnectPoints.h"

#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "Core/PCGExProbingCandidates.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Helpers/PCGExArrayHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExConnectPointsElement"
#define PCGEX_NAMESPACE BuildCustomGraph

TArray<FPCGPinProperties> UPCGExConnectPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceProbesLabel, "Probes used to connect points", Required, FPCGExDataTypeInfoProbe::AsId())

	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterGenerators, "Points that don't meet requirements won't generate connections", Normal)
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterConnectables, "Points that don't meet requirements can't receive connections", Normal)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExConnectPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ConnectPoints)
PCGEX_ELEMENT_BATCH_POINT_IMPL(ConnectPoints)

bool FPCGExConnectPointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ConnectPoints)

	if (!PCGExFactories::GetInputFactories<UPCGExProbeFactoryData>(Context, PCGExClusters::Labels::SourceProbesLabel, Context->ProbeFactories, {FPCGExDataTypeInfoProbe::AsId()}))
	{
		return false;
	}

	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterGenerators, Context->GeneratorsFiltersFactories, PCGExFactories::PointFilters(), false);
	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterConnectables, Context->ConnectablesFiltersFactories, PCGExFactories::PointFilters(), false);

	Context->CWCoincidenceTolerance = FVector(Settings->CoincidenceTolerance);

	return true;
}

bool FPCGExConnectPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExConnectPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ConnectPoints)
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
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters. Make sure inputs have at least 2 points."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExConnectPoints
{
	FProcessor::~FProcessor()
	{
	}

	void FProcessor::AppendEdges(const TSet<uint64>& InUniqueEdges)
	{
		FWriteScopeLock WriteScopeLock(UniqueEdgesLock);
		UniqueEdges.Reserve(UniqueEdges.Num() + InUniqueEdges.Num());
		UniqueEdges.Append(InUniqueEdges);
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExConnectPoints::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		CWCoincidenceTolerance = PCGEx::SafeTolerance(Context->CWCoincidenceTolerance);
		bPreventCoincidence = Settings->bPreventCoincidence;

		if (Settings->bProjectPoints)
		{
			ProjectionDetails = Settings->ProjectionDetails;
			ProjectionDetails.Init(PointDataFacade);
		}

		CanGenerate.SetNumUninitialized(NumPoints);
		AcceptConnections.SetNumUninitialized(NumPoints);

		PCGExArrayHelpers::InitArray(WorkingTransforms, NumPoints);
		PCGExArrayHelpers::InitArray(WorkingPositions, NumPoints);

		AllOperations.Reserve(Context->ProbeFactories.Num());

		for (const UPCGExProbeFactoryData* Factory : Context->ProbeFactories)
		{
			TSharedPtr<FPCGExProbeOperation> NewOperation = Factory->CreateOperation(Context);
			NewOperation->BindContext(ExecutionContext);
			NewOperation->PrimaryDataFacade = PointDataFacade;

			NewOperation->WorkingTransforms = &WorkingTransforms;
			NewOperation->WorkingPositions = &WorkingPositions;
			NewOperation->CanGenerate = &CanGenerate;
			NewOperation->AcceptConnections = &AcceptConnections;

			if (!NewOperation->Prepare(Context))
			{
				continue;
			}

			AllOperations.Add(NewOperation);

			if (NewOperation->WantsOctree())
			{
				bWantsOctree = true;
			}

			if (NewOperation->IsGlobalProbe())
			{
				GlobalOperations.Add(NewOperation.Get());
				continue;
			}

			if (NewOperation->IsDirectProbe())
			{
				DirectOperations.Add(NewOperation.Get());
				continue;
			}

			if (!NewOperation->SearchRadius->IsConstant())
			{
				bUseVariableRadius = true;
			}
			SharedSearchRadius = FMath::Max(SharedSearchRadius, NewOperation->BaseConfig->SearchRadiusConstant);

			if (NewOperation->RequiresChainProcessing())
			{
				ChainedOperations.Add(NewOperation.Get());
			}
			else
			{
				SharedOperations.Add(NewOperation.Get());
			}

			RadiusSources.Add(NewOperation.Get());
		}

		NumRadiusSources = RadiusSources.Num();
		NumChainedOps = ChainedOperations.Num();
		NumSharedOps = SharedOperations.Num();
		NumDirectOps = DirectOperations.Num();
		NumGlobalOps = GlobalOperations.Num();

		if (!RadiusSources.IsEmpty())
		{
			bWantsOctree = true;
		}

		bOnlyGlobalOps = RadiusSources.IsEmpty() && DirectOperations.IsEmpty();

		if (bOnlyGlobalOps && GlobalOperations.IsEmpty())
		{
			return false;
		}

		if (!PointDataFacade->Source->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			return false;
		}
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

		if (!Context->GeneratorsFiltersFactories.IsEmpty())
		{
			GeneratorsFilter = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!GeneratorsFilter->Init(ExecutionContext, Context->GeneratorsFiltersFactories))
			{
				return false;
			}
		}

		if (!Context->ConnectablesFiltersFactories.IsEmpty())
		{
			ConnectableFilter = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!ConnectableFilter->Init(ExecutionContext, Context->ConnectablesFiltersFactories))
			{
				return false;
			}
		}

		bUseProjection = Settings->bProjectPoints;

		if (bWantsOctree)
		{
			// If we have search probes, build the octree
			const FBox B = PointDataFacade->GetIn()->GetBounds();
			Octree = MakeUnique<PCGExOctree::FItemOctree>(bUseProjection ? ProjectionDetails.ProjectFlat(B.GetCenter()) : B.GetCenter(), B.GetExtent().Length());
		}

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, PrepTask)

		PrepTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnPreparationComplete();
		};

		PrepTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->PointDataFacade->Fetch(Scope);

			PCGEX_SCOPE_LOOP(i)
			{
				This->CanGenerate[i] = This->GeneratorsFilter ? This->GeneratorsFilter->Test(i) : true;
				This->AcceptConnections[i] = This->ConnectableFilter ? This->ConnectableFilter->Test(i) : true;
			}
		};

		PrepTask->StartSubLoops(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());

		return true;
	}

	void FProcessor::OnPreparationComplete()
	{
		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		const TConstPCGValueRange<FTransform> OriginalTransforms = InPointData->GetConstTransformValueRange();

		const int32 NumPoints = OriginalTransforms.Num();

		PCGExMT::ParallelOrSequential(
			NumPoints,
			[&](const int32 i)
			{
				if (bUseProjection)
				{
					WorkingTransforms[i] = ProjectionDetails.ProjectFlat(OriginalTransforms[i]);
					WorkingPositions[i] = WorkingTransforms[i].GetLocation();
				}
				else
				{
					WorkingTransforms[i] = OriginalTransforms[i];
					WorkingPositions[i] = OriginalTransforms[i].GetLocation();
				}
			});

		if (bWantsOctree)
		{
			constexpr double PPRefRadius = 0.05;
			const FVector PPRefExtents = FVector(PPRefRadius);

			if (bUseProjection)
			{
				for (int i = 0; i < NumPoints; i++)
				{
					if (!AcceptConnections[i])
					{
						continue;
					}
					Octree->AddElement(PCGExOctree::FItem(i, FBoxSphereBounds(WorkingPositions[i], PPRefExtents, PPRefRadius)));
				}
			}
			else
			{
				for (int i = 0; i < NumPoints; i++)
				{
					if (!AcceptConnections[i])
					{
						continue;
					}
					Octree->AddElement(PCGExOctree::FItem(i, FBoxSphereBounds(WorkingPositions[i], PPRefExtents, PPRefRadius)));
				}
			}

			for (const TSharedPtr<FPCGExProbeOperation>& Operation : AllOperations)
			{
				Operation->Octree = Octree.Get();
			}
		}

		GeneratorsFilter.Reset();
		ConnectableFilter.Reset();

		NumCompletions = GlobalOperations.IsEmpty() ? 0 : 1;
		if (!bOnlyGlobalOps)
		{
			NumCompletions++;
			StartParallelLoopForPoints(PCGExData::EIOSide::In);
		}

		if (!GlobalOperations.IsEmpty())
		{
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, GlobalOpsTasks)
			GlobalOpsTasks->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->AdvanceCompletion();
			};

			for (FPCGExProbeOperation* Operation : GlobalOperations)
			{
				GlobalOpsTasks->AddSimpleCallback([PCGEX_ASYNC_THIS_CAPTURE, Op = Operation]()
				{
					PCGEX_ASYNC_THIS
					TSet<uint64> LocalEdges;
					Op->ProcessAll(LocalEdges);
					if (!LocalEdges.IsEmpty())
					{
						This->AppendEdges(LocalEdges);
					}
				});
			}

			GlobalOpsTasks->StartSimpleCallbacks();
		}
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		IProcessor::PrepareLoopScopesForPoints(Loops);
		ScopedEdges = MakeShared<PCGExMT::TScopedSet<uint64>>(Loops, 10);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::ConnectPoints::ProcessPoints);

		TSet<uint64>* LocalUniqueEdges = ScopedEdges->Get(Scope).Get();
		TUniquePtr<TSet<uint64>> LocalCoincidence;
		if (bPreventCoincidence)
		{
			LocalCoincidence = MakeUnique<TSet<uint64>>();
		}

#define PCGEX_SCOPED_CONTAINERS(_NAME)\
		TArray<TSharedPtr<PCGExMT::FScopedContainer>> _NAME##OpsContainers;\
		_NAME##OpsContainers.Reserve(NumChainedOps);\
		for (int i = 0; i < Num##_NAME##Ops; i++){ _NAME##OpsContainers.Add(_NAME##Operations[i]->GetScopedContainer(Scope)); }

		PCGEX_SCOPED_CONTAINERS(Chained)
		PCGEX_SCOPED_CONTAINERS(Shared)
		PCGEX_SCOPED_CONTAINERS(Direct)

#undef PCGEX_SCOPED_CONTAINERS

		TArray<PCGExProbing::FCandidate> Candidates;
		TArray<PCGExProbing::FBestCandidate> BestCandidates;

		FVector Origin = FVector::ZeroVector;
		int32 CurrentIndex = 0;

		auto ProcessPoint = [&](const PCGExOctree::FItem& InPositionRef)
		{
			const int32 OtherPointIndex = InPositionRef.Index;
			if (OtherPointIndex == CurrentIndex)
			{
				return;
			}

			const FVector Position = WorkingPositions[OtherPointIndex];
			const FVector Dir = (Origin - Position).GetSafeNormal();
			const int32 EmplaceIndex = Candidates.Emplace(OtherPointIndex, Dir, FVector::DistSquared(Position, Origin), bPreventCoincidence ? PCGEx::SH3(Dir, CWCoincidenceTolerance) : 0);

			if (NumChainedOps > 0)
			{
				for (int i = 0; i < NumChainedOps; i++)
				{
					ChainedOperations[i]->ProcessCandidateChained(i, EmplaceIndex, Candidates[EmplaceIndex], BestCandidates[i], ChainedOpsContainers[i].Get());
				}
			}
		};

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!CanGenerate[Index])
			{
				continue;
			} // Not a generator

#define PCGEX_SCOPED_CONTAINERS_RESET(_NAME) for (const TSharedPtr<PCGExMT::FScopedContainer>& Container : _NAME##OpsContainers){ if(Container){ Container->Reset(); } }

			PCGEX_SCOPED_CONTAINERS_RESET(Chained)
			PCGEX_SCOPED_CONTAINERS_RESET(Shared)
			PCGEX_SCOPED_CONTAINERS_RESET(Direct)

#undef PCGEX_SCOPED_CONTAINERS_RESET

			CurrentIndex = Index;
			Candidates.Reset();

			if (LocalCoincidence)
			{
				LocalCoincidence->Reset();
			}

			if (NumChainedOps > 0)
			{
				BestCandidates.SetNum(NumChainedOps);
				for (int i = 0; i < NumChainedOps; i++)
				{
					ChainedOperations[i]->PrepareBestCandidate(Index, BestCandidates[i], ChainedOpsContainers[i].Get());
				}
			}

			if (!RadiusSources.IsEmpty())
			{
				double MaxRadius = 0;
				if (!bUseVariableRadius)
				{
					MaxRadius = SharedSearchRadius;
				}
				else
				{
					for (int i = 0; i < NumRadiusSources; i++)
					{
						MaxRadius = FMath::Max(MaxRadius, RadiusSources[i]->GetSearchRadius(Index));
					}
				}

				Origin = WorkingPositions[Index];

				// Find candidates within radius
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Origin, FVector(MaxRadius)), ProcessPoint);
				Candidates.Sort([&](const PCGExProbing::FCandidate& A, const PCGExProbing::FCandidate& B)
				{
					return A.Distance < B.Distance;
				});

				for (int i = 0; i < NumChainedOps; i++)
				{
					ChainedOperations[i]->ProcessBestCandidate(Index, BestCandidates[i], Candidates, LocalCoincidence.Get(), CWCoincidenceTolerance, LocalUniqueEdges, ChainedOpsContainers[i].Get());
				}

				for (int i = 0; i < NumSharedOps; i++)
				{
					SharedOperations[i]->ProcessCandidates(Index, Candidates, LocalCoincidence.Get(), CWCoincidenceTolerance, LocalUniqueEdges, SharedOpsContainers[i].Get());
				}
			}

			for (int i = 0; i < NumDirectOps; i++)
			{
				DirectOperations[i]->ProcessNode(Index, LocalCoincidence.Get(), CWCoincidenceTolerance, LocalUniqueEdges, DirectOpsContainers[i].Get());
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		{
			FWriteScopeLock WriteScopeLock(UniqueEdgesLock);
			ScopedEdges->Collapse(UniqueEdges);
		}

		ScopedEdges.Reset();

		AdvanceCompletion();
	}

	void FProcessor::AdvanceCompletion()
	{
		if (FPlatformAtomics::InterlockedDecrement(&NumCompletions))
		{
			return;
		}

		GraphBuilder->Graph->InsertEdges_Unsafe(UniqueEdges, -1);
		GraphBuilder->CompileAsync(TaskManager, true);
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder->bCompiledSuccessfully)
		{
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
		}
	}

	void FProcessor::Output()
	{
		GraphBuilder->StageEdgesOutputs();
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExConnectPointsContext, UPCGExConnectPointsSettings>::Cleanup();
		AllOperations.Empty();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
