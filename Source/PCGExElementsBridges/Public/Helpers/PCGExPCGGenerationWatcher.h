// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGComponent.h"

#include "PCGExPCGGenerationWatcher.generated.h"

class FPCGExIntTracker;

namespace PCGExMT
{
	class FAsyncToken;
	class FTaskManager;
}

UENUM()
enum class EPCGExGenerationTriggerAction : uint8
{
	Ignore        = 0 UMETA(DisplayName = "Ignore", ToolTip="Ignore component if not actively generating already"),
	AsIs          = 1 UMETA(DisplayName = "As-is", ToolTip="Grab the data as-is and doesnt'try to generate if it wasn't."),
	Generate      = 2 UMETA(DisplayName = "Generate", ToolTip="Generate and wait for completion. If the component was already generated, this should not trigger a regeneration."),
	ForceGenerate = 3 UMETA(DisplayName = "Generate (force)", ToolTip="Generate (force) and wait for completion. Already generated component will be re-regenerated."),
};

UENUM()
enum class EPCGExRuntimeGenerationTriggerAction : uint8
{
	Ignore       = 0 UMETA(DisplayName = "Ignore", ToolTip="Ignore component if not actively generating already"),
	AsIs         = 1 UMETA(DisplayName = "As-is", ToolTip="Grab the data as-is and doesnt'try to refresh it."),
	RefreshFirst = 2 UMETA(DisplayName = "Refresh", ToolTip="Refresh and wait for completion"),
};

namespace PCGExPCGInterop
{
	/** Generation trigger action configuration */
	struct PCGEXELEMENTSBRIDGES_API FGenerationConfig
	{
		EPCGExGenerationTriggerAction GenerateOnLoadAction = EPCGExGenerationTriggerAction::Generate;
		EPCGExGenerationTriggerAction GenerateOnDemandAction = EPCGExGenerationTriggerAction::Generate;
		EPCGExRuntimeGenerationTriggerAction GenerateAtRuntimeAction = EPCGExRuntimeGenerationTriggerAction::AsIs;

		bool ShouldIgnore(EPCGComponentGenerationTrigger Trigger) const;
		bool TriggerGeneration(UPCGComponent* Component, bool& bOutShouldWatch) const;
	};

	/**
	 * Watches PCG components for generation completion.
	 * Handles triggering generation and waiting for completion via callbacks.
	 */
	class PCGEXELEMENTSBRIDGES_API FGenerationWatcher final : public TSharedFromThis<FGenerationWatcher>
	{
	public:
		using FOnGenerationComplete = TFunction<void(UPCGComponent*, bool bSuccess)>;

		FGenerationWatcher(
			const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
			const FGenerationConfig& InGenerationConfig);

		~FGenerationWatcher();

		void SetOnGenerationComplete(FOnGenerationComplete&& InCallback) { OnGenerationComplete = MoveTemp(InCallback); }
		void SetOnAllComplete(TFunction<void()>&& InCallback) { OnAllComplete = MoveTemp(InCallback); }

		/** Must be called after construction to initialize the tracker (cannot use SharedThis in constructor) */
		void Initialize();

		void Watch(UPCGComponent* InComponent);

	private:
		void ProcessComponent(UPCGComponent* InComponent);
		void WatchComponentGeneration(UPCGComponent* InComponent);
		void OnComponentReady(UPCGComponent* InComponent, bool bSuccess);

		TWeakPtr<PCGExMT::FTaskManager> TaskManagerWeak;
		FGenerationConfig GenerationConfig;

		TWeakPtr<PCGExMT::FAsyncToken> WatchToken;
		TSharedPtr<FPCGExIntTracker> WatcherTracker;

		FOnGenerationComplete OnGenerationComplete;
		TFunction<void()> OnAllComplete;
	};
}
