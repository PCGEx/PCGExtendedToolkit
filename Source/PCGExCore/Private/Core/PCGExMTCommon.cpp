// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExMTCommon.h"
#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"

namespace PCGExMT
{
#pragma region FScope

	FScope::FScope(const int32 InStart, const int32 InCount, const int32 InLoopIndex)
		: Start(InStart)
		  , Count(InCount)
		  , End(InStart + InCount)
		  , LoopIndex(InLoopIndex)
	{
	}

	void FScope::GetIndices(TArray<int32>& OutIndices) const
	{
		OutIndices.SetNumUninitialized(Count);
		for (int i = 0; i < Count; i++)
		{
			OutIndices[i] = Start + i;
		}
	}

#pragma endregion

#pragma region Parallel Helpers

	void ParallelOrSequential(const int32 Num, const FLoopBody& Body, const int32 Threshold, const EParallelForFlags Flags)
	{
		if (Num >= Threshold)
		{
			ParallelFor(Num, Body, Flags);
		}
		else
		{
			for (int32 i = 0; i < Num; ++i)
			{
				Body(i);
			}
		}
	}

	void ParallelOrSequentialScoped(const int32 Num, const FScopedLoopBody& Body, const int32 Threshold)
	{
		if (Num <= 0)
		{
			return;
		}

		if (Num >= Threshold)
		{
			// Divide into one chunk per worker thread -- amortizes Body's per-scope setup
			// (e.g., FScopedTypedValue construction for type-erased buffer access).
			// ParallelFor handles work-stealing across the resulting chunks.
			const int32 NumWorkers = FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads());
			const int32 ChunkSize = FMath::Max(1, FMath::DivideAndRoundUp(Num, NumWorkers));
			const int32 NumChunks = FMath::DivideAndRoundUp(Num, ChunkSize);

			ParallelFor(NumChunks, [&](const int32 ChunkIdx)
			{
				const int32 Start = ChunkIdx * ChunkSize;
				const int32 Count = FMath::Min(ChunkSize, Num - Start);
				const FScope Scope(Start, Count, ChunkIdx);
				Body(Scope);
			});
		}
		else
		{
			// Sequential: single scope covering everything.
			const FScope Scope(0, Num, 0);
			Body(Scope);
		}
	}

	void Sequential(const int32 Num, const FLoopBody& Body)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			Body(i);
		}
	}

#pragma endregion
}
