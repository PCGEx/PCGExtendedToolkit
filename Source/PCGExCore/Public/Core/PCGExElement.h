// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"

#define PCGEX_EXECUTION_CHECK_C(_CONTEXT) if(!_CONTEXT->CanExecute()){ return true; }
#define PCGEX_EXECUTION_CHECK PCGEX_EXECUTION_CHECK_C(Context)
#define PCGEX_ASYNC_WAIT_C(_CONTEXT) if (_CONTEXT->bWaitingForAsyncCompletion) { return false; }
#define PCGEX_ASYNC_WAIT PCGEX_ASYNC_WAIT_C(Context)
#define PCGEX_ASYNC_WAIT_INTERNAL if (bWaitingForAsyncCompletion) { return false; }
#define PCGEX_ON_STATE(_STATE) if(Context->IsState(_STATE))
#define PCGEX_ON_STATE_INTERNAL(_STATE) if(IsState(_STATE))
#define PCGEX_ON_ASYNC_STATE_READY(_STATE) if(Context->IsState(_STATE) && Context->IsWaitingForTasks()){ return false; }else if(Context->IsState(_STATE) && !Context->IsWaitingForTasks())
#define PCGEX_ON_ASYNC_STATE_READY_INTERNAL(_STATE) if(const bool ShouldWait = IsWaitingForTasks(); IsState(_STATE) && ShouldWait){ return false; }else if(IsState(_STATE) && !ShouldWait)
#define PCGEX_ON_INITIAL_EXECUTION if(Context->IsInitialExecution())
#define PCGEX_POINTS_BATCH_PROCESSING(_STATE) if (!Context->ProcessPointsBatch(_STATE)) { return false; }

#define PCGEX_ELEMENT_CREATE_CONTEXT(_CLASS) virtual FPCGContext* CreateContext() override { return new FPCGEx##_CLASS##Context(); }
#define PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT virtual FPCGContext* CreateContext() override { return new FPCGExContext(); }

class UPCGExSettings;
struct FPCGExContext;

class PCGEXCORE_API IPCGExElement : public IPCGElement
{
	friend struct FPCGExContext;

public:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool AdvancePreparation(FPCGExContext* Context, const UPCGExSettings* InSettings) const;
	virtual FPCGContext* Initialize(const FPCGInitializeElementParams& InParams) override;

#if WITH_EDITOR
	virtual bool ShouldLog() const override
	{
		return false;
	}
#endif

	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;

	/** Full content CRC when stealing, so downstream detects the in-place mutation (identity is unchanged). */
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;

	virtual void OnContextInitialized(FPCGExContext* InContext) const;

	virtual bool Boot(FPCGExContext* InContext) const;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const;
	virtual bool PostBoot(FPCGExContext* InContext) const;
	virtual void AbortInternal(FPCGContext* Context) const override;

	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override;

	/** Elements that populate FPCGContext::DynamicDependencies (ScheduleGraph + bIsPaused) must return
	 *  false: the executor registers paused-task successors from the dependencies visible when
	 *  ExecuteInternal returns, so dependencies added from a detached AdvanceWork are never watched
	 *  and the node is never unpaused. */
	virtual bool SupportsDetachedExecute() const { return true; }

	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual void InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const;
	virtual void CompleteWork(FPCGExContext* InContext) const;
};
