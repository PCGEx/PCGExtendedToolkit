// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMT.h"

namespace PCGExMT
{
	/**
	 * Runs a predicate on the game thread, once per subsystem tick, until it returns true or a real-time timeout
	 * elapses; OnCompleteCallback then fires on the game thread, unless the work was cancelled.
	 * Being a registered handle, it keeps the PCG context paused for the whole wait: an unpaused node that keeps
	 * postponing itself is re-picked by the PCG executor ahead of every other game-thread task.
	 * Register it from AdvanceWork with the context in State_WaitingOnAsyncWork and return false; its completion
	 * re-drives AdvanceWork inline. The owner must keep it alive: the task manager only holds it weakly.
	 */
	class PCGEXCORE_API FMainThreadPoll final : public IExecuteOnMainThread
	{
	public:
		/** Game thread. True ends the wait. */
		using FPollCallback = std::function<bool()>;
		FPollCallback OnPollCallback;

		explicit FMainThreadPoll(const double InTimeoutSeconds);

		virtual bool Start() override;

		/** Meaningful from OnCompleteCallback onward. */
		bool HasTimedOut() const { return bTimedOut; }

	protected:
		double TimeoutSeconds = 0;
		double Deadline = 0;
		bool bTimedOut = false;

		virtual bool Execute() override;
	};
}
