// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExPCGGenerationWatcher.h"

#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "Core/PCGExMT.h"
#include "Utils/PCGExIntTracker.h"

#pragma region FGenerationConfig

bool PCGExPCGInterop::FGenerationConfig::ShouldIgnore(EPCGComponentGenerationTrigger Trigger) const
{
	switch (Trigger)
	{
	case EPCGComponentGenerationTrigger::GenerateOnLoad:
		return GenerateOnLoadAction == EPCGExGenerationTriggerAction::Ignore;
	case EPCGComponentGenerationTrigger::GenerateOnDemand:
		return GenerateOnDemandAction == EPCGExGenerationTriggerAction::Ignore;
	case EPCGComponentGenerationTrigger::GenerateAtRuntime:
		return GenerateAtRuntimeAction == EPCGExRuntimeGenerationTriggerAction::Ignore;
	default:
		return true;
	}
}

bool PCGExPCGInterop::FGenerationConfig::TriggerGeneration(UPCGComponent* Component, bool& bOutShouldWatch) const
{
	bOutShouldWatch = false;

	if (Component->IsCleaningUp()) { return false; }

	// Already generating - just watch
	if (Component->IsGenerating())
	{
		bOutShouldWatch = true;
		return true;
	}

	bool bForce = false;

	switch (Component->GenerationTrigger)
	{
	case EPCGComponentGenerationTrigger::GenerateOnLoad:
		switch (GenerateOnLoadAction)
		{
		case EPCGExGenerationTriggerAction::AsIs:
			return true; // Data ready
		case EPCGExGenerationTriggerAction::ForceGenerate:
			bForce = true;
			[[fallthrough]];
		case EPCGExGenerationTriggerAction::Generate:
			Component->Generate(bForce);
			bOutShouldWatch = true;
			return true;
		default:
			return false;
		}

	case EPCGComponentGenerationTrigger::GenerateOnDemand:
		switch (GenerateOnDemandAction)
		{
		case EPCGExGenerationTriggerAction::AsIs:
			return true;
		case EPCGExGenerationTriggerAction::ForceGenerate:
			bForce = true;
			[[fallthrough]];
		case EPCGExGenerationTriggerAction::Generate:
			Component->Generate(bForce);
			bOutShouldWatch = true;
			return true;
		default:
			return false;
		}

	case EPCGComponentGenerationTrigger::GenerateAtRuntime:
		switch (GenerateAtRuntimeAction)
		{
		case EPCGExRuntimeGenerationTriggerAction::AsIs:
			return true;
		case EPCGExRuntimeGenerationTriggerAction::RefreshFirst:
			if (UPCGSubsystem* PCGSubsystem = UPCGSubsystem::GetSubsystemForCurrentWorld())
			{
				PCGSubsystem->RefreshRuntimeGenComponent(Component, EPCGChangeType::GenerationGrid);
				bOutShouldWatch = true;
				return true;
			}
			return false;
		default:
			return false;
		}

	default:
		return false;
	}
}

#pragma endregion

#pragma region FGenerationWatcher

PCGExPCGInterop::FGenerationWatcher::FGenerationWatcher(
	const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
	const FGenerationConfig& InGenerationConfig)
	: TaskManagerWeak(InTaskManager)
	  , GenerationConfig(InGenerationConfig)
{
}

void PCGExPCGInterop::FGenerationWatcher::Initialize()
{
	// Must be called after construction since SharedThis requires fully constructed object
	WatcherTracker = MakeShared<FPCGExIntTracker>(
		[WeakThis = TWeakPtr<FGenerationWatcher>(SharedThis(this))]()
		{
			// On first pending - create watch token
			if (TSharedPtr<FGenerationWatcher> This = WeakThis.Pin())
			{
				if (TSharedPtr<PCGExMT::FTaskManager> TaskManager = This->TaskManagerWeak.Pin())
				{
					This->WatchToken = TaskManager->TryCreateToken(FName("Watch"));
				}
			}
		},
		[WeakThis = TWeakPtr<FGenerationWatcher>(SharedThis(this))]()
		{
			// On all complete - release token and notify
			if (TSharedPtr<FGenerationWatcher> This = WeakThis.Pin())
			{
				PCGEX_ASYNC_RELEASE_CAPTURED_TOKEN(This->WatchToken)
				if (This->OnAllComplete)
				{
					This->OnAllComplete();
				}
			}
		});
}

PCGExPCGInterop::FGenerationWatcher::~FGenerationWatcher()
{
	PCGEX_ASYNC_RELEASE_TOKEN(WatchToken)
}

void PCGExPCGInterop::FGenerationWatcher::Watch(UPCGComponent* InComponent)
{
	if (GenerationConfig.ShouldIgnore(InComponent->GenerationTrigger)) { return; }

	WatcherTracker->IncrementPending();
	ProcessComponent(InComponent);
}

void PCGExPCGInterop::FGenerationWatcher::ProcessComponent(UPCGComponent* InComponent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPCGInterop::ProcessComponent);

	bool bShouldWatch = false;
	if (!GenerationConfig.TriggerGeneration(InComponent, bShouldWatch))
	{
		// Failed to trigger or ignored
		WatcherTracker->IncrementCompleted();
		return;
	}

	if (bShouldWatch)
	{
		WatchComponentGeneration(InComponent);
	}
	else
	{
		// Data is ready immediately
		OnComponentReady(InComponent, true);
	}
}

void PCGExPCGInterop::FGenerationWatcher::WatchComponentGeneration(UPCGComponent* InComponent)
{
	if (!InComponent->IsGenerating())
	{
		OnComponentReady(InComponent, true);
		return;
	}

	TWeakPtr<FGenerationWatcher> WeakWatcher = SharedThis(this);
	TWeakObjectPtr<UPCGComponent> WeakComponent = InComponent;

	PCGExMT::ExecuteOnMainThread([WeakWatcher, WeakComponent]()
	{
		TSharedPtr<FGenerationWatcher> Watcher = WeakWatcher.Pin();
		UPCGComponent* Component = WeakComponent.Get();

		if (!Watcher || !Component)
		{
			if (Watcher) { Watcher->WatcherTracker->IncrementCompleted(); }
			return;
		}

		if (!Component->IsGenerating())
		{
			Watcher->OnComponentReady(Component, true);
			return;
		}

		// Watch for cancellation
		Component->OnPCGGraphCancelledDelegate.AddLambda([WeakWatcher](UPCGComponent* InComp)
		{
			if (TSharedPtr<FGenerationWatcher> NestedWatcher = WeakWatcher.Pin())
			{
				NestedWatcher->OnComponentReady(InComp, false);
			}
		});

		// Watch for completion
		Component->OnPCGGraphGeneratedDelegate.AddLambda([WeakWatcher](UPCGComponent* InComp)
		{
			if (TSharedPtr<FGenerationWatcher> NestedWatcher = WeakWatcher.Pin())
			{
				NestedWatcher->OnComponentReady(InComp, true);
			}
		});
	});
}

void PCGExPCGInterop::FGenerationWatcher::OnComponentReady(UPCGComponent* InComponent, bool bSuccess)
{
	if (OnGenerationComplete)
	{
		OnGenerationComplete(InComponent, bSuccess);
	}

	WatcherTracker->IncrementCompleted();
}

#pragma endregion
