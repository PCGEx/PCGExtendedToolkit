// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExPCGGenerationWatcher.h"

#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "Core/PCGExMT.h"
#include "GameFramework/Actor.h"
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

bool PCGExPCGInterop::FGenerationConfig::TriggerGeneration(UPCGComponent* Component, bool& bOutShouldWatch, UPCGComponent* InSelf, AActor*& OutIgnoredOwner) const
{
	bOutShouldWatch = false;
	OutIgnoredOwner = nullptr;

	// Before we kick a source generation, tell our own component to discard change
	// notifications originating from that source for the duration of our generation.
	// Without this the source's completion broadcast re-enters FPCGActorAndComponentMapping
	// and cancels our in-flight execution -> game-thread hang, or torn-read crash mid-duplicate.
	// Mirrors the engine's PCGDataFromActor node; the change can fire synchronously inside
	// Generate(), so the bracket must open first. Owner actor because OnPCGGraphGeneratedOrCleaned
	// reports the change on the owner.
	auto BeginIgnore = [&]()
	{
#if WITH_EDITOR
		if (InSelf)
		{
			if (UPCGComponent* SelfOriginal = InSelf->GetOriginalComponent())
			{
				if (AActor* Owner = Component->GetOwner())
				{
					SelfOriginal->StartIgnoringChangeOriginDuringGeneration(Owner);
					OutIgnoredOwner = Owner;
				}
			}
		}
#endif
	};
#if !WITH_EDITOR
	(void)InSelf; // Ignore brackets are editor-only; InSelf is otherwise unused.
#endif

	if (Component->IsCleaningUp())
	{
		return false;
	}

	// Already generating - just watch. We still listen for its completion broadcast, which can
	// re-enter and cancel us, so bracket it too even though we didn't trigger the generation.
	if (Component->IsGenerating())
	{
		BeginIgnore();
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
			BeginIgnore();
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
			BeginIgnore();
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
				PCGSubsystem->RefreshRuntimeGenExecutionSource(Component, EPCGChangeType::GenerationGrid);
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
	const FGenerationConfig& InGenerationConfig,
	const TWeakObjectPtr<UPCGComponent>& InSelf)
	: SelfWeak(InSelf)
	  , TaskManagerWeak(InTaskManager)
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
#if WITH_EDITOR
	// Sweep any brackets still open (cancellation / teardown before completion fired).
	ReleaseIgnoredOrigins(nullptr);
#endif
	PCGEX_ASYNC_RELEASE_TOKEN(WatchToken)
}

#if WITH_EDITOR
void PCGExPCGInterop::FGenerationWatcher::ReleaseIgnoredOrigins(UPCGComponent* ForSource)
{
	for (int32 i = IgnoredOrigins.Num() - 1; i >= 0; --i)
	{
		const FIgnoredOrigin& Entry = IgnoredOrigins[i];
		if (ForSource && Entry.Source.Get() != ForSource)
		{
			continue;
		}

		if (UPCGComponent* Self = Entry.Self.Get())
		{
			if (UPCGComponent* SelfOriginal = Self->GetOriginalComponent())
			{
				if (AActor* Owner = Entry.Owner.Get())
				{
					// Only close the bracket while it's still live. The engine auto-resets a component's
					// ignore map when its generation ends (PostProcessGraph / OnProcessGraphAborted), and
					// our generation routinely ends before the source's completion/cancel delegate fires
					// (or before this watcher is destroyed). The entry is then already gone, so an unguarded
					// Stop trips the engine's ensure(FoundCounter) -> breakpoint (0x80000003).
					if (SelfOriginal->IsIgnoringChangeOrigin(Owner))
					{
						SelfOriginal->StopIgnoringChangeOriginDuringGeneration(Owner);
					}
				}
			}
		}

		IgnoredOrigins.RemoveAt(i);
	}
}
#endif

void PCGExPCGInterop::FGenerationWatcher::Watch(UPCGComponent* InComponent)
{
	if (GenerationConfig.ShouldIgnore(InComponent->GenerationTrigger))
	{
		return;
	}

	WatcherTracker->IncrementPending();
	ProcessComponent(InComponent);
}

void PCGExPCGInterop::FGenerationWatcher::ProcessComponent(UPCGComponent* InComponent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPCGInterop::ProcessComponent);

	bool bShouldWatch = false;
	AActor* IgnoredOwner = nullptr;
	if (!GenerationConfig.TriggerGeneration(InComponent, bShouldWatch, SelfWeak.Get(), IgnoredOwner))
	{
		// Failed to trigger or ignored
		WatcherTracker->IncrementCompleted();
		return;
	}

#if WITH_EDITOR
	if (IgnoredOwner)
	{
		IgnoredOrigins.Add({SelfWeak, IgnoredOwner, InComponent});
	}
#endif

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
			if (Watcher)
			{
				Watcher->WatcherTracker->IncrementCompleted();
			}
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
#if WITH_EDITOR
	// Source generation finished broadcasting; safe to stop ignoring its change origin.
	ReleaseIgnoredOrigins(InComponent);
#endif

	if (OnGenerationComplete)
	{
		OnGenerationComplete(InComponent, bSuccess);
	}

	WatcherTracker->IncrementCompleted();
}

#pragma endregion
