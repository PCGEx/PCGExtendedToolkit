// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExElement.h"

#include "PCGExCoreSettingsCache.h"
#include "RHITransientResourceAllocator.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExWaitMacros.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

bool IPCGExElement::PrepareDataInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::PrepareDataInternal)

	check(Context);

	FPCGExContext* InContext = static_cast<FPCGExContext*>(Context);

	const UPCGExSettings* InSettings = Context->GetInputSettings<UPCGExSettings>();
	check(InSettings);

	if (IsInGameThread()
		&& InSettings->GetForceOffThreadPrepare(InContext)
		&& !InContext->bPreparationDispatchedOffThread
		&& !CanExecuteOnlyOnMainThread(InContext))
	{
		InContext->bPreparationDispatchedOffThread = true;
		InContext->PauseContext();

		TWeakPtr<FPCGContextHandle> WeakHandle = InContext->GetWeakSelfHandle();
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, WeakHandle, InSettings, PinTracker = InContext->GetAsyncPinTracker()]()
		{
			// Count this drive as holding a context pin, incremented before the pin below.
			PCGExMT::FAsyncContextPinScope PinScope(PinTracker);
			FPCGContext::FSharedContext<FPCGExContext> Pinned(WeakHandle);
			if (FPCGExContext* Ctx = Pinned.Get())
			{
				(void)AdvancePreparation(Ctx, InSettings);
			}
		});

		return false;
	}

	return AdvancePreparation(InContext, InSettings);
}

bool IPCGExElement::AdvancePreparation(FPCGExContext* Context, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::AdvancePreparation)

	if (!Context->GetInputSettings<UPCGSettings>()->bEnabled)
	{
		return Context->CancelExecution(FString());
	}

	PCGEX_EXECUTION_CHECK_C(Context)

	// Preparation is a multi-phase state machine:
	// 1. Boot: validate inputs, configure context
	// 2. Register & load asset dependencies (may pause for async loading)
	// 3. PostLoadAssetsDependencies: finalize setup after assets are available
	// 4. PostBoot: last chance setup before execution begins
	// Each PCGEX_ON_ASYNC_STATE_READY gate re-enters when the async state completes,
	// returning false to yield to the scheduler in the meantime.
	if (Context->IsState(PCGExCommon::States::State_Preparation))
	{
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::InitializeData::Boot)
			if (!Boot(Context))
			{
				return Context->CancelExecution(FString());
			}
		}

		// Many nodes cancel from their Boot() override by writing:
		// {
		//     return Context->CancelExecution(TEXT("..."));
		// }
		// and FPCGExContext::CancelExecution() returns true. 
		// so Boot() returns true even though it just cancelled the context. 
		// The `if (!Boot())` above does not catch that, so re-check here: 
		// if Boot cancelled (or completed) execution, finalize now. Otherwise
		// we would fall through to RegisterAssetDependencies/LoadAssets and re-pause an already-cancelled context,
		// whose async completion (OnAsyncWorkEnd) refuses to resume it -> permanently wedged in PrepareData.
		PCGEX_EXECUTION_CHECK_C(Context)

		for (UPCGExInstancedFactory* Op : Context->InternalOperations)
		{
			Op->RegisterAssetDependencies(Context);
		}

		Context->RegisterAssetDependencies();
		if (Context->HasAssetRequirements() && Context->LoadAssets())
		{
			// Cold cache: an async load was dispatched -- yield now. The State_LoadingAssetDependencies
			// gate below fires PostLoadAssetsDependencies exactly once when the load completes. A warm-cache
			// hit (bWarmDependencies) completed synchronously and falls through to that same gate in this
			// tick, so the hook still runs exactly once -- never twice.
			if (!Context->bWarmDependencies)
			{
				return false;
			}
		}
		else
		{
			// No async dependency load was scheduled (no requirements), so the
			// State_LoadingAssetDependencies gate will not fire -- run the post-load hook directly, once.
			PostLoadAssetsDependencies(Context);
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_LoadingAssetDependencies)
	{
		PostLoadAssetsDependencies(Context);
		PCGEX_EXECUTION_CHECK_C(Context)
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_AsyncPreparation)
	{
		PCGEX_EXECUTION_CHECK_C(Context)
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::InitializeData::PostBoot)
		if (!PostBoot(Context))
		{
			return Context->CancelExecution(TEXT("There was a problem during post-data preparation."));
		}
	}

	Context->ReadyForExecution();
	return true;
}

FPCGContext* IPCGExElement::Initialize(const FPCGInitializeElementParams& InParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::Initialize)

	FPCGExContext* Context = static_cast<FPCGExContext*>(IPCGElement::Initialize(InParams));

	const UPCGExSettings* Settings = Context->GetInputSettings<UPCGExSettings>();
	check(Settings);

	Context->bFlattenOutput = Settings->bFlattenOutput;
	Context->bScopedAttributeGet = Settings->WantsScopedAttributeGet();
	Context->bPropagateAbortedExecution = Settings->bPropagateAbortedExecution;

	Context->bQuietInvalidInputWarning = Settings->bQuietInvalidInputWarning;
	Context->bQuietMissingInputError = Settings->bQuietMissingInputError;
	Context->bQuietCancellationError = Settings->bQuietCancellationError;
	Context->bCleanupConsumableAttributes = Settings->bCleanupConsumableAttributes;

	Context->bWantsDataStealing = Settings->WantsDataStealing();
	Context->bWantsResourcesCached = Settings->WantsResourcesCached();

	Context->ElementHandle = this;

	if (Context->bCleanupConsumableAttributes)
	{
		for (const TArray<FString> Names = PCGExArrayHelpers::GetStringArrayFromCommaSeparatedList(Settings->CommaSeparatedProtectedAttributesName);
		     const FString& Name : Names)
		{
			Context->AddProtectedAttributeName(FName(Name));
		}

		for (const FName& Name : Settings->ProtectedAttributes)
		{
			Context->AddProtectedAttributeName(FName(Name));
		}
	}

	OnContextInitialized(Context);

	return Context;
}

bool IPCGExElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGExSettings* Settings = static_cast<const UPCGExSettings*>(InSettings);
	return !Settings->WantsDataStealing() && Settings->ShouldCache();
}

FPCGContext* IPCGExElement::CreateContext()
{
	return new FPCGExContext();
}

void IPCGExElement::OnContextInitialized(FPCGExContext* InContext) const
{
	InContext->SetState(PCGExCommon::States::State_Preparation);
}

bool IPCGExElement::Boot(FPCGExContext* InContext) const
{
	if (InContext->InputData.bCancelExecution)
	{
		return false;
	}
	return true;
}

void IPCGExElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
}

bool IPCGExElement::PostBoot(FPCGExContext* InContext) const
{
	return true;
}

void IPCGExElement::AbortInternal(FPCGContext* Context) const
{
	IPCGElement::AbortInternal(Context);

	if (!Context)
	{
		return;
	}

	//UE_LOG(LogTemp, Warning, TEXT(">> ABORTING @%s"), *Context->GetInputSettings<UPCGExSettings>()->GetName());

	FPCGExContext* PCGExContext = static_cast<FPCGExContext*>(Context);
	PCGExContext->CancelExecution();
}

bool IPCGExElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	return false;
}

bool IPCGExElement::SupportsBasePointDataInputs(FPCGContext* InContext) const
{
	return true;
}

bool IPCGExElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::ExecuteInternal)

	check(Context);

	FPCGExContext* InContext = static_cast<FPCGExContext*>(Context);

	PCGEX_EXECUTION_CHECK_C(InContext)

	const UPCGExSettings* InSettings = Context->GetInputSettings<UPCGExSettings>();
	check(InSettings);

	if (InContext->IsInitialExecution())
	{
		InitializeData(InContext, InSettings);
	}

	if (IsInGameThread()
		&& (InSettings->GetForceOffThreadExecute(InContext))
		&& !InContext->bExecutionDispatchedOffThread
		&& !CanExecuteOnlyOnMainThread(InContext))
	{
		InContext->bExecutionDispatchedOffThread = true;
		InContext->PauseContext();

		TWeakPtr<FPCGContextHandle> WeakHandle = InContext->GetWeakSelfHandle();
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, WeakHandle, InSettings, PinTracker = InContext->GetAsyncPinTracker()]()
		{
			// Count this drive as holding a context pin, incremented before the pin below.
			PCGExMT::FAsyncContextPinScope PinScope(PinTracker);
			FPCGContext::FSharedContext<FPCGExContext> Pinned(WeakHandle);
			if (FPCGExContext* Ctx = Pinned.Get())
			{
				(void)Ctx->DriveAdvanceWork(InSettings);
			}
		});

		return false;
	}

	return InContext->DriveAdvanceWork(InSettings);
}

void IPCGExElement::InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPCGExElement::InitializeData)

	const FPCGStack* Stack = InContext->GetStack();
	if (!ensure(Stack))
	{
		PCGE_LOG_C(Error, LogOnly, InContext, LOCTEXT("ContextHasNoExecutionStack", "The execution context is malformed and has no call stack."));
		return;
	}

	// Extract loop indices from the PCG execution stack to determine if this node
	// is running inside a loop. LoopIndex is the immediate parent loop (second-to-last frame),
	// TopLoopIndex is the outermost loop in the stack. These affect execution policy decisions
	// (e.g. NoPauseButLoop only spin-waits when inside a loop to avoid per-iteration frame delays).
	const TArray<FPCGStackFrame>& StackFrames = Stack->GetStackFrames();

	if (StackFrames.Num() >= 2)
	{
		InContext->LoopIndex = StackFrames.Last(1).LoopIndex;
	}

	for (const FPCGStackFrame& Frame : StackFrames)
	{
		if (Frame.LoopIndex != INDEX_NONE)
		{
			InContext->TopLoopIndex = Frame.LoopIndex;
			break;
		}
	}
}

bool IPCGExElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	return true;
}

void IPCGExElement::CompleteWork(FPCGExContext* InContext) const
{
	const UPCGExSettings* InSettings = InContext->GetInputSettings<UPCGExSettings>();
	check(InSettings);
}

#undef LOCTEXT_NAMESPACE
