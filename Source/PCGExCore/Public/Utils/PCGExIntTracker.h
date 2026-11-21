// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"

#include <atomic>

class PCGEXCORE_API FPCGExIntTracker final : public TSharedFromThis<FPCGExIntTracker>
{
	// Net in-flight work (pending minus completed); reaching exactly 0 fires the threshold.
	// bTriggered is a one-shot latch so it fires at most once per Reset cycle.
	std::atomic<int32> Outstanding{0};
	std::atomic<bool> bTriggered{false};

	TFunction<void()> StartFn = nullptr;
	TFunction<void()> ThresholdFn = nullptr;

public:
	explicit FPCGExIntTracker(TFunction<void()>&& InThresholdFn)
	{
		ThresholdFn = InThresholdFn;
	}

	explicit FPCGExIntTracker(TFunction<void()>&& InStartFn, TFunction<void()>&& InThresholdFn)
	{
		StartFn = InStartFn;
		ThresholdFn = InThresholdFn;
	}

	~FPCGExIntTracker() = default;

	void IncrementPending(const int32 Count = 1);
	void IncrementCompleted(const int32 Count = 1);

	void Reset();
	void Reset(const int32 InMax);

protected:
	void TriggerInternal();
};
