// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExMainThreadPoll.h"

#include "HAL/PlatformTime.h"

namespace PCGExMT
{
	FMainThreadPoll::FMainThreadPoll(const double InTimeoutSeconds)
		: TimeoutSeconds(FMath::Max(0.0, InTimeoutSeconds))
	{
	}

	bool FMainThreadPoll::Start()
	{
		check(OnPollCallback)

		// Real time, not world time: that one needs a world and stops while the game is paused.
		Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		return IExecuteOnMainThread::Start();
	}

	bool FMainThreadPoll::Execute()
	{
		if (IsCancelled() || OnPollCallback()) { return true; }

		bTimedOut = FPlatformTime::Seconds() >= Deadline;
		return bTimedOut;
	}
}
