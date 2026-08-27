// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPointsMT.h"

#include "UObject/Class.h"

#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Factories/PCGExInstancedFactory.h"

namespace PCGExPointsMT
{
#pragma region Tasks

	template <typename T>
	class FStartBatchProcessing final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FStartBatchProcessing)

		FStartBatchProcessing(TSharedPtr<T> InTarget)
			: FTask()
			  , Target(InTarget)
		{
		}

		TSharedPtr<T> Target;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			Target->Process(TaskManager);
		}
	};

#pragma endregion

	IProcessor::IProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
		: PointDataFacade(InPointDataFacade)
	{
	}

	void IProcessor::SetExecutionContext(FPCGExContext* InContext)
	{
		check(InContext)
		ExecutionContext = InContext;
		WorkHandle = ExecutionContext->GetWorkHandle();
	}

	void IProcessor::SetPointsFilterData(TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFactories)
	{
		FilterFactories = InFactories;
	}

	void IProcessor::RegisterConsumableAttributesWithFacade() const
	{
		// Gives an opportunity for the processor to register attributes with a valid facade
		// So selectors shortcut can be properly resolved (@Last, etc.)

		if (FilterFactories)
		{
			PCGExFactories::RegisterConsumableAttributesWithFacade(*FilterFactories, PointDataFacade);
		}
		if (PrimaryInstancedFactory)
		{
			PrimaryInstancedFactory->RegisterConsumableAttributesWithFacade(ExecutionContext, PointDataFacade);
		}
	}

	void IProcessor::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		if (HasFilters())
		{
			PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, *FilterFactories, FacadePreloader);
		}
	}

	void IProcessor::PrefetchData(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const TSharedPtr<PCGExMT::FTaskGroup>& InPrefetchDataTaskGroup)
	{
		TaskManager = InTaskManager;

		InternalFacadePreloader = MakeShared<PCGExData::FFacadePreloader>(PointDataFacade);
		RegisterBuffersDependencies(*InternalFacadePreloader);

		InternalFacadePreloader->StartLoading(TaskManager, InPrefetchDataTaskGroup);
	}

	bool IProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TaskManager = InTaskManager;
		PCGEX_ASYNC_CHKD(TaskManager)

#pragma region Primary filters

		// Honour the return value, as the cluster path does (PCGExClusterMT.cpp:340): a filter that fails
		// to bind otherwise leaves an empty stack behind, and the processor runs unfiltered.
		if (FilterFactories && !InitPrimaryFilters(FilterFactories))
		{
			return false;
		}

#pragma endregion

		if (PrimaryInstancedFactory)
		{
			if (PrimaryInstancedFactory->WantsPerDataInstance())
			{
				PrimaryInstancedFactory = PrimaryInstancedFactory->CreateNewInstance(ExecutionContext->ManagedObjects.Get());
				if (!PrimaryInstancedFactory)
				{
					return false;
				}
				PrimaryInstancedFactory->PrimaryDataFacade = PointDataFacade;
			}
		}

		return true;
	}

	void IProcessor::StartParallelLoopForPoints(const PCGExData::EIOSide Side, const int32 PerLoopIterations)
	{
		const UPCGBasePointData* CurrentProcessingSource = const_cast<UPCGBasePointData*>(PointDataFacade->GetData(Side));
		if (!CurrentProcessingSource)
		{
			return;
		}

		const int32 NumPoints = CurrentProcessingSource->GetNumPoints();

		PCGEX_CHECK_WORK_HANDLE_VOID

		if (IsTrivial())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(StartParallelLoopForPoints_Trivial);
			const PCGExMT::FScope TrivialScope(0, NumPoints, 0);
			PrepareLoopScopesForPoints({TrivialScope});
			ProcessPoints(TrivialScope);
			OnPointsProcessingComplete();
			return;
		}

		PCGEX_ENSURE_NONZERO_LOOP_RANGE(NumPoints, StartParallelLoopForPoints);

		TRACE_CPUPROFILER_EVENT_SCOPE(StartParallelLoopForPoints);

		const int32 PLI = PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize(PerLoopIterations);

		TArray<PCGExMT::FScope> Loops;
		const int32 NumScopes = PCGExMT::SubLoopScopes(
			Loops, NumPoints, FMath::Max(1, PCGExMT::GetSanitizedBatchSize(NumPoints, PLI)));

		PrepareLoopScopesForPoints(Loops);

		if (NumScopes == 1 || bForceSingleThreadedProcessPoints)
		{
			for (const PCGExMT::FScope& S : Loops)
			{
				if (!WorkHandle.IsValid())
				{
					break;
				}
				ProcessPoints(S);
			}
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				NumScopes,
				[this, &Loops](const int32 i)
				{
					if (!WorkHandle.IsValid())
					{
						return;
					}
					ProcessPoints(Loops[i]);
				},
				/*Threshold=*/2, EParallelForFlags::Unbalanced);
		}

		OnPointsProcessingComplete();
	}

	void IProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
	}

	void IProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
	}

	void IProcessor::OnPointsProcessingComplete()
	{
	}

	void IProcessor::StartParallelLoopForRange(const int32 NumIterations, const int32 PerLoopIterations)
	{
		PCGEX_CHECK_WORK_HANDLE_VOID

		if (IsTrivial())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(StartParallelLoopForRanges_Trivial);
			const PCGExMT::FScope TrivialScope(0, NumIterations, 0);
			PrepareLoopScopesForRanges({TrivialScope});
			ProcessRange(TrivialScope);
			OnRangeProcessingComplete();
			return;
		}

		PCGEX_ENSURE_NONZERO_LOOP_RANGE(NumIterations, StartParallelLoopForRange);

		TRACE_CPUPROFILER_EVENT_SCOPE(StartParallelLoopForRanges);

		const int32 PLI = PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize(PerLoopIterations);

		TArray<PCGExMT::FScope> Loops;
		const int32 NumScopes = PCGExMT::SubLoopScopes(
			Loops, NumIterations, FMath::Max(1, PCGExMT::GetSanitizedBatchSize(NumIterations, PLI)));

		PrepareLoopScopesForRanges(Loops);

		if (NumScopes == 1 || bForceSingleThreadedProcessRange)
		{
			for (const PCGExMT::FScope& S : Loops)
			{
				if (!WorkHandle.IsValid())
				{
					break;
				}
				ProcessRange(S);
			}
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				NumScopes,
				[this, &Loops](const int32 i)
				{
					if (!WorkHandle.IsValid())
					{
						return;
					}
					ProcessRange(Loops[i]);
				},
				/*Threshold=*/2, EParallelForFlags::Unbalanced);
		}

		OnRangeProcessingComplete();
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
		bIsProcessorValid = false;
	}

	bool IProcessor::InitPrimaryFilters(const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
	{
		PointFilterCache.Init(DefaultPointFilterValue, PointDataFacade->GetNum());

		if (InFilterFactories->IsEmpty())
		{
			return true;
		}

		PrimaryFilters = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
		return PrimaryFilters->Init(ExecutionContext, *InFilterFactories);
	}

	int32 IProcessor::FilterScope(const PCGExMT::FScope& Scope, const bool bParallel)
	{
		if (PrimaryFilters)
		{
			return PrimaryFilters->Test(Scope, PointFilterCache, bParallel);
		}
		return DefaultPointFilterValue ? Scope.Count : 0;
	}

	int32 IProcessor::FilterAll()
	{
		return FilterScope(PCGExMT::FScope(0, PointDataFacade->GetNum()), true);
	}

	TSharedPtr<IProcessor> IBatch::NewProcessorInstance(const TSharedRef<PCGExData::FFacade>& InPointDataFacade) const
	{
		return nullptr;
	}

	IBatch::IBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
		: ExecutionContext(InContext),
		  PointsCollection(InPointsCollection)
	{
		SetExecutionContext(InContext);
	}

	void IBatch::SetExecutionContext(FPCGExContext* InContext)
	{
		ExecutionContext = InContext;
		WorkHandle = ExecutionContext->GetWorkHandle();
	}

	bool IBatch::PrepareProcessing()
	{
		return true;
	}

	void IBatch::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPointsMT::IBatch::Process);

		if (PointsCollection.IsEmpty())
		{
			return;
		}

		TaskManager = InTaskManager;
		PCGEX_ASYNC_CHKD_VOID(TaskManager)

		TSharedPtr<IBatch> SelfPtr = SharedThis(this);

		const bool bDoInitData = DataInitializationPolicy == PCGExData::EIOInit::Duplicate || DataInitializationPolicy == PCGExData::EIOInit::New;

		TArray<TSharedPtr<IProcessor>> Candidates;
		Candidates.SetNum(PointsCollection.Num());

		PCGExMT::ParallelOrSequential(
			Candidates.Num(),
			[&](const int32 i)
			{
				TSharedPtr<PCGExData::FPointIO> IO = PointsCollection[i].Pin();

				PCGEX_MAKE_SHARED(PointDataFacade, PCGExData::FFacade, IO.ToSharedRef())

				const TSharedPtr<IProcessor> NewProcessor = NewProcessorInstance(PointDataFacade.ToSharedRef());

				NewProcessor->SetExecutionContext(ExecutionContext);
				NewProcessor->ParentBatch = SharedThis(this);
				NewProcessor->BatchIndex = i;

				if (FilterFactories)
				{
					NewProcessor->SetPointsFilterData(FilterFactories);
				}
				if (PrimaryInstancedFactory)
				{
					NewProcessor->PrimaryInstancedFactory = PrimaryInstancedFactory;
				}

				NewProcessor->RegisterConsumableAttributesWithFacade();

				if (!PrepareSingle(NewProcessor.ToSharedRef()))
				{
					return;
				}

				NewProcessor->bIsTrivial = IO->GetNum() < PCGEX_CORE_SETTINGS.SmallPointsSize;
				Candidates[i] = NewProcessor;

			}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		{
			Processors.Reserve(Candidates.Num());
			ProcessorFacades.Reserve(Candidates.Num());
			SubProcessorMap->Reserve(Candidates.Num());

			int Pi = 0;
			for (const TSharedPtr<IProcessor>& C : Candidates)
			{
				if (C)
				{
					TSharedRef<IProcessor>& P = Processors.Add_GetRef(C.ToSharedRef());
					P->BatchIndex = Pi++;
					ProcessorFacades.Add(P->PointDataFacade);
					SubProcessorMap->Add(&P->PointDataFacade->Source.Get(), P);

					if (bDoInitData)
					{
						P->PointDataFacade->Source->InitializeOutput(DataInitializationPolicy);
					}
				}
			}
		}

		if (Processors.IsEmpty())
		{
			return;
		}

		if (bPrefetchData)
		{
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ParallelAttributeRead)

			ParallelAttributeRead->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->OnProcessingPreparationComplete();
			};

			ParallelAttributeRead->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, ParallelAttributeRead](const int32 Index, const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				This->Processors[Index]->PrefetchData(This->TaskManager, ParallelAttributeRead);
			};

			ParallelAttributeRead->StartIterations(Processors.Num(), 1);
		}
		else
		{
			OnProcessingPreparationComplete();
		}
	}

	void IBatch::OnInitialPostProcess()
	{
	}

	bool IBatch::PrepareSingle(const TSharedRef<IProcessor>& InProcessor)
	{
		return true;
	}

	void IBatch::CompleteWork()
	{
		if (bSkipCompletion)
		{
			return;
		}
		//PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(CompleteWork, bForceSingleThreadedCompletion, { Processor->CompleteWork(); }, {})
		PCGEX_CHECK_WORK_HANDLE_VOID
		if (bForceSingleThreadedCompletion)
		{
			for (TSharedRef<IProcessor>& Processor : Processors)
			{
				if (Processor->bIsProcessorValid)
				{
					Processor->CompleteWork();
				}
			}
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				Processors.Num(),
				[&](const int32 i)
				{
					const TSharedRef<IProcessor>& Processor = Processors[i];
					if (Processor->bIsProcessorValid)
					{
						Processor->CompleteWork();
					}
				}, /*Threshold=*/2, EParallelForFlags::Unbalanced);
		}
	}

	void IBatch::Write()
	{
		//PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(Write, bForceSingleThreadedWrite, { Processor->Write(); }, {})
		PCGEX_CHECK_WORK_HANDLE_VOID
		if (bForceSingleThreadedWrite)
		{
			for (TSharedRef<IProcessor>& Processor : Processors)
			{
				if (Processor->bIsProcessorValid)
				{
					Processor->Write();
				}
			}
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				Processors.Num(),
				[&](const int32 i)
				{
					const TSharedRef<IProcessor>& Processor = Processors[i];
					if (Processor->bIsProcessorValid)
					{
						Processor->Write();
					}
				}, /*Threshold=*/2, EParallelForFlags::Unbalanced);
		}
	}

	void IBatch::Output()
	{
		for (const TSharedRef<IProcessor>& P : Processors)
		{
			if (!P->bIsProcessorValid)
			{
				continue;
			}
			P->Output();
		}
	}

	void IBatch::Cleanup()
	{
		ProcessorFacades.Empty();

		for (const TSharedRef<IProcessor>& P : Processors)
		{
			P->Cleanup();
		}
		Processors.Empty();
	}

	void IBatch::OnProcessingPreparationComplete()
	{
		//	PCGEX_ASYNC_MT_LOOP_TPL(Process, bForceSingleThreadedProcessing, { Processor->bIsProcessorValid = Processor->Process(This->TaskManager); }, { Process->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE](){ PCGEX_ASYNC_THIS This->OnInitialPostProcess(); };})
		PCGEX_CHECK_WORK_HANDLE_VOID

		if (bForceSingleThreadedProcessing)
		{
			for (TSharedRef<IProcessor>& Processor : Processors)
			{
				Processor->bIsProcessorValid = Processor->Process(TaskManager);
			}
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				Processors.Num(),
				[&](const int32 i)
				{
					const TSharedRef<IProcessor>& Processor = Processors[i];
					Processor->bIsProcessorValid = Processor->Process(TaskManager);
				}, /*Threshold=*/2, EParallelForFlags::Unbalanced);
		}

		OnInitialPostProcess();
	}

	void ScheduleBatch(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<IBatch>& Batch)
	{
		PCGEX_LAUNCH(FStartBatchProcessing<IBatch>, Batch)
	}
}
