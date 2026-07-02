// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/ControlFlow/PCGExUberBranch.h"

#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExUberBranch"
#define PCGEX_NAMESPACE UberBranch

#if WITH_EDITOR
void UPCGExUberBranchSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	InputLabels.Reset(NumBranches);
	OutputLabels.Reset(NumBranches);

	for (int i = 0; i < NumBranches; i++)
	{
		FString SI = FString::Printf(TEXT("%d"), i + 0);
		InputLabels.Emplace(FName(TEXT("→ ") + SI));
		OutputLabels.Emplace(FName(SI + TEXT(" →")));
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UPCGExUberBranchSettings::HasDynamicPins() const
{
	return true;
}

TArray<FPCGPinProperties> UPCGExUberBranchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainInputPin(), "The data to be processed.", Required)

	for (int i = 0; i < NumBranches; i++)
	{
		PCGEX_PIN_FILTERS(InputLabels[i], "Collection filters. Only support Data Filter or regular filters that are set-up to work with data bounds or @Data attributes.", Normal)
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExUberBranchSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainOutputPin(), "Collections that didn't branch in any specific pin", Normal)

	for (int i = 0; i < NumBranches; i++)
	{
		PCGEX_PIN_ANY(OutputLabels[i], "Collections that passed the matching input filters, if they weren't output to any previous pin.", Normal)
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberBranch)

FName UPCGExUberBranchSettings::GetMainOutputPin() const
{
	// Ensure proper forward when node is disabled
	return FName("Default");
}

bool FPCGExUberBranchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(UberBranch)

	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		IO->InitializeOutput(PCGExData::EIOInit::Forward);
		PCGEX_MAKE_SHARED(Facade, PCGExData::FFacade, IO.ToSharedRef())
		Context->Facades.Add(Facade);
	}

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		bool bInitialized = false;

		if (TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> Factories;
			PCGExFactories::GetInputFactories(Context, Settings->InputLabels[i], Factories, PCGExFactories::PointFilters()))
		{
			for (const TSharedPtr<PCGExData::FFacade>& Facade : Context->Facades)
			{
				// Attempt to initialize with all data until hopefully one works
				PCGEX_MAKE_SHARED(Manager, PCGExPointFilter::FManager, Facade.ToSharedRef())
				Manager->bWillBeUsedWithCollections = true;
				bInitialized = Manager->Init(Context, Factories);
				if (bInitialized)
				{
					Context->Managers.Add(Manager);
					break;
				}
			}
		}

		if (!bInitialized)
		{
			Context->Managers.Add(nullptr);
		}
	}

	Context->Dispatch.Init(0, Settings->NumBranches);

	return true;
}

bool FPCGExUberBranchElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberBranchElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberBranch)
	PCGEX_EXECUTION_CHECK
	if (Settings->AsyncChunkSize > 0)
	{
		PCGEX_ON_INITIAL_EXECUTION
		{
			TWeakPtr<FPCGContextHandle> Handle = Context->GetWeakSelfHandle();

			Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);
			PCGEX_ASYNC_GROUP_CHKD_RET(Context->GetTaskManager(), BranchTask, true)

			BranchTask->OnSubLoopStartCallback = [Handle, Settings](const PCGExMT::FScope& Scope)
			{
				PCGEX_SHARED_TCONTEXT_VOID(UberBranch, Handle)
				PCGEX_SCOPE_LOOP(Index)
				{
					const TSharedPtr<PCGExData::FFacade>& Facade = SharedContext.Get()->Facades[Index];

					bool bDistributed = false;
					for (int i = 0; i < Settings->NumBranches; i++)
					{
						const TSharedPtr<PCGExPointFilter::FManager> Manager = SharedContext.Get()->Managers[i];
						if (!Manager)
						{
							continue;
						}
						Manager->bWillBeUsedWithCollections = true;
						if (Manager->Test(Facade->Source, SharedContext.Get()->MainPoints))
						{
							Facade->Source->OutputPin = Settings->OutputLabels[i];
							FPlatformAtomics::InterlockedIncrement(&SharedContext.Get()->Dispatch[i]);
							bDistributed = true;
							break;
						}
					}

					if (!bDistributed)
					{
						Facade->Source->OutputPin = Settings->GetMainOutputPin();
					}
				}
			};

			BranchTask->StartSubLoops(Context->Facades.Num(), Settings->AsyncChunkSize);
			return false;
		}

		PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
		{
			for (int i = 0; i < Settings->NumBranches; i++)
			{
				if (!Context->Dispatch[i])
				{
					Context->OutputData.InactiveOutputPinBitmask |= 1ULL << (i + 1);
				}
			}
			Context->MainPoints->StageOutputs();
			Context->Done();
		}
	}
	else
	{
		for (const TSharedPtr<PCGExData::FFacade>& Facade : Context->Facades)
		{
			bool bDistributed = false;
			for (int i = 0; i < Settings->NumBranches; i++)
			{
				const TSharedPtr<PCGExPointFilter::FManager> Manager = Context->Managers[i];
				if (!Manager)
				{
					continue;
				}
				if (Manager->Test(Facade->Source, Context->MainPoints))
				{
					Facade->Source->OutputPin = Settings->OutputLabels[i];
					Context->Dispatch[i]++;
					bDistributed = true;
					break;
				}
			}

			if (!bDistributed)
			{
				Facade->Source->OutputPin = Settings->GetMainOutputPin();
			}
		}

		for (int i = 0; i < Settings->NumBranches; i++)
		{
			if (!Context->Dispatch[i])
			{
				Context->OutputData.InactiveOutputPinBitmask |= 1ULL << (i + 1);
			}
		}
		Context->MainPoints->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
