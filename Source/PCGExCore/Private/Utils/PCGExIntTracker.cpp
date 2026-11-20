// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Utils/PCGExIntTracker.h"
#include "CoreMinimal.h"

void FPCGExIntTracker::IncrementPending(const int32 Count)
{
	// Once latched the tracker is inert until Reset; a stale 'false' read is harmless.
	if (bTriggered.load(std::memory_order_relaxed)) { return; }

	// Only one thread can observe a previous value of 0, so StartFn fires exactly once.
	if (Outstanding.fetch_add(Count, std::memory_order_acq_rel) == 0 && StartFn)
	{
		StartFn();
	}
}

void FPCGExIntTracker::IncrementCompleted(const int32 Count)
{
	if (bTriggered.load(std::memory_order_relaxed)) { return; }

	// The thread that drives the net count to exactly 0 fires the threshold; acq_rel
	// guarantees it sees every other worker's completed writes.
	if (Outstanding.fetch_sub(Count, std::memory_order_acq_rel) - Count == 0)
	{
		TriggerInternal();
	}
}

void FPCGExIntTracker::Reset()
{
	// Phase boundary -- must not race with in-flight increments.
	Outstanding.store(0, std::memory_order_release);
	bTriggered.store(false, std::memory_order_release);
}

void FPCGExIntTracker::Reset(const int32 InMax)
{
	Outstanding.store(InMax, std::memory_order_release);
	bTriggered.store(false, std::memory_order_release);
}

void FPCGExIntTracker::TriggerInternal()
{
	// One-shot: only the first caller to flip the latch runs the threshold.
	if (bTriggered.exchange(true, std::memory_order_acq_rel)) { return; }

	ThresholdFn();
	Outstanding.store(0, std::memory_order_release);
}
