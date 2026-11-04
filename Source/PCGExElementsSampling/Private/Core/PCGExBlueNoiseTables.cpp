// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlueNoiseTables.h"

#include <atomic>

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Tasks/Task.h"

namespace PCGExBlueNoise
{
	namespace Build
	{
		constexpr int32 PoolSize = TileNumPoints * 4;
		constexpr uint64 TileSeed = 0xD5A61266F0C9392Cull;
		constexpr int32 PoolCellSize = 256;
		constexpr int32 PoolGridSide = TileUnits / PoolCellSize;

		constexpr uint64 MaskSeed = 0x9E3779B97F4A7C15ull;
		constexpr int32 MaskCells = DitherMaskSize * DitherMaskSize;
		constexpr int32 MaskInitialOnes = MaskCells / 10;
		constexpr int32 KernelReach = 8;

		/** Gaussian weights by squared torus distance (sigma 1.5, x 2^20, rounded); zero past the last entry. Baked, not
		 *  computed, so no platform's exp() can round them differently. */
		constexpr int64 KernelWeights[] = {
			1048576, 839634, 672326, 538357, 431082, 345184, 276402, 221325, 177223, 141909, 113632, 90989,
			72859, 58341, 46716, 37407, 29953, 23985, 19205, 15378, 12314, 9860, 7896, 6322,
			5062, 4054, 3246, 2599, 2081, 1667, 1334, 1069, 856, 685, 549, 439,
			352, 282, 226, 181, 145, 116, 93, 74, 59, 48, 38, 31,
			24, 20, 16, 13, 10, 8, 6, 5, 4, 3, 3, 2,
			2, 1, 1, 1, 1, 1,
		};

		constexpr int32 KernelMaxDistanceSquared = static_cast<int32>(UE_ARRAY_COUNT(KernelWeights)) - 1;
		static_assert((KernelReach + 1) * (KernelReach + 1) > KernelMaxDistanceSquared, "Kernel reach misses weighted cells");

		uint16 TileXY[TileNumPoints * 2];
		uint16 DitherMask[MaskCells];
		std::atomic<bool> bBuilt{false};
		FCriticalSection BuildLock;
		UE::Tasks::FTask PrebuildTask;

		struct FSplitMix64
		{
			uint64 State = 0;

			explicit FSplitMix64(const uint64 InSeed)
				: State(InSeed)
			{
			}

			uint64 Next()
			{
				State += 0x9E3779B97F4A7C15ull;
				uint64 Z = State;
				Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
				Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
				return Z ^ (Z >> 31);
			}

			int32 Below(const int32 InCount)
			{
				return static_cast<int32>(Next() % static_cast<uint64>(InCount));
			}
		};

		int64 TorusDistanceSquared(const int32 AX, const int32 AY, const int32 BX, const int32 BY)
		{
			int32 DX = FMath::Abs(AX - BX);
			int32 DY = FMath::Abs(AY - BY);
			DX = FMath::Min(DX, TileUnits - DX);
			DY = FMath::Min(DY, TileUnits - DY);
			return static_cast<int64>(DX) * DX + static_cast<int64>(DY) * DY;
		}

		/** Max-heap of pool indices keyed by distance, lower index first on ties, with slots so a key can drop in place. */
		class FIndexedHeap
		{
		public:
			explicit FIndexedHeap(const TArray<int64>& InKeys)
				: Keys(InKeys)
			{
				Slots.Init(INDEX_NONE, InKeys.Num());
			}

			void Add(const int32 InIndex)
			{
				Slots[InIndex] = Heap.Num();
				Heap.Add(InIndex);
			}

			void Heapify()
			{
				for (int32 Slot = Heap.Num() / 2 - 1; Slot >= 0; Slot--)
				{
					SiftDown(Slot);
				}
			}

			int32 Pop()
			{
				const int32 Top = Heap[0];
				Slots[Top] = INDEX_NONE;

				const int32 Last = Heap.Pop(EAllowShrinking::No);
				if (!Heap.IsEmpty())
				{
					Place(0, Last);
					SiftDown(0);
				}

				return Top;
			}

			void KeyDecreased(const int32 InIndex)
			{
				SiftDown(Slots[InIndex]);
			}

		private:
			const TArray<int64>& Keys;
			TArray<int32> Heap;
			TArray<int32> Slots;

			bool Above(const int32 A, const int32 B) const
			{
				return Keys[A] > Keys[B] || (Keys[A] == Keys[B] && A < B);
			}

			void Place(const int32 InSlot, const int32 InIndex)
			{
				Heap[InSlot] = InIndex;
				Slots[InIndex] = InSlot;
			}

			void SiftDown(int32 Slot)
			{
				const int32 Num = Heap.Num();
				const int32 Index = Heap[Slot];

				while (true)
				{
					const int32 Left = Slot * 2 + 1;
					if (Left >= Num)
					{
						break;
					}

					const int32 Right = Left + 1;
					const int32 Child = Right < Num && Above(Heap[Right], Heap[Left]) ? Right : Left;
					if (!Above(Heap[Child], Index))
					{
						break;
					}

					Place(Slot, Heap[Child]);
					Slot = Child;
				}

				Place(Slot, Index);
			}
		};

		void ApplyKernel(TArray<int64>& InOutEnergy, const int32 InCell, const int64 InSign)
		{
			const int32 X = InCell % DitherMaskSize;
			const int32 Y = InCell / DitherMaskSize;

			for (int32 DY = -KernelReach; DY <= KernelReach; DY++)
			{
				const int32 Row = ((Y + DY + DitherMaskSize) % DitherMaskSize) * DitherMaskSize;
				for (int32 DX = -KernelReach; DX <= KernelReach; DX++)
				{
					const int32 DistanceSquared = DX * DX + DY * DY;
					if (DistanceSquared <= KernelMaxDistanceSquared)
					{
						InOutEnergy[Row + (X + DX + DitherMaskSize) % DitherMaskSize] += InSign * KernelWeights[DistanceSquared];
					}
				}
			}
		}

		/** First one-cell in row-major order with the most energy. */
		int32 FindTightestCluster(const TArray<uint8>& InPattern, const TArray<int64>& InEnergy)
		{
			int32 Best = INDEX_NONE;
			for (int32 Cell = 0; Cell < MaskCells; Cell++)
			{
				if (InPattern[Cell] && (Best == INDEX_NONE || InEnergy[Cell] > InEnergy[Best]))
				{
					Best = Cell;
				}
			}
			return Best;
		}

		/** First zero-cell in row-major order with the least energy. */
		int32 FindLargestVoid(const TArray<uint8>& InPattern, const TArray<int64>& InEnergy)
		{
			int32 Best = INDEX_NONE;
			for (int32 Cell = 0; Cell < MaskCells; Cell++)
			{
				if (!InPattern[Cell] && (Best == INDEX_NONE || InEnergy[Cell] < InEnergy[Best]))
				{
					Best = Cell;
				}
			}
			return Best;
		}

		void EnsureBuilt()
		{
			if (bBuilt.load(std::memory_order_acquire))
			{
				return;
			}

			FScopeLock Lock(&BuildLock);
			if (bBuilt.load(std::memory_order_relaxed))
			{
				return;
			}

			BuildTile(MakeArrayView(TileXY));
			BuildMask(MakeArrayView(DitherMask));
			bBuilt.store(true, std::memory_order_release);
		}
	}

	void BuildTile(const TArrayView<uint16> OutTileXY)
	{
		using namespace Build;
		check(OutTileXY.Num() == TileNumPoints * 2);

		FSplitMix64 Random(TileSeed);
		TArray<int32> PoolX;
		TArray<int32> PoolY;
		PoolX.SetNumUninitialized(PoolSize);
		PoolY.SetNumUninitialized(PoolSize);
		for (int32 i = 0; i < PoolSize; i++) { PoolX[i] = Random.Below(TileUnits); }
		for (int32 i = 0; i < PoolSize; i++) { PoolY[i] = Random.Below(TileUnits); }

		// Pool points per grid cell, as a counting sort.
		TArray<int32> CellStart;
		CellStart.Init(0, PoolGridSide * PoolGridSide + 1);
		TArray<int32> CellOf;
		CellOf.SetNumUninitialized(PoolSize);
		for (int32 i = 0; i < PoolSize; i++)
		{
			CellOf[i] = (PoolY[i] / PoolCellSize) * PoolGridSide + PoolX[i] / PoolCellSize;
			CellStart[CellOf[i] + 1]++;
		}

		for (int32 c = 0; c < PoolGridSide * PoolGridSide; c++)
		{
			CellStart[c + 1] += CellStart[c];
		}

		TArray<int32> CellItems;
		CellItems.SetNumUninitialized(PoolSize);
		{
			TArray<int32> Cursor(CellStart);
			for (int32 i = 0; i < PoolSize; i++)
			{
				CellItems[Cursor[CellOf[i]]++] = i;
			}
		}

		// Each pool point's squared distance to its nearest pick; the first pick is pool point 0.
		TArray<int64> Distance;
		Distance.SetNumUninitialized(PoolSize);
		TArray<uint8> Taken;
		Taken.Init(0, PoolSize);

		TArray<int32> Order;
		Order.Reserve(TileNumPoints);
		Order.Add(0);
		Taken[0] = 1;
		Distance[0] = 0;

		FIndexedHeap Heap(Distance);
		for (int32 q = 1; q < PoolSize; q++)
		{
			Distance[q] = TorusDistanceSquared(PoolX[0], PoolY[0], PoolX[q], PoolY[q]);
			Heap.Add(q);
		}
		Heap.Heapify();

		auto Relax = [&](const int32 InPick, const int32 InCandidate)
		{
			if (Taken[InCandidate])
			{
				return;
			}

			const int64 DistanceSquared = TorusDistanceSquared(PoolX[InPick], PoolY[InPick], PoolX[InCandidate], PoolY[InCandidate]);
			if (DistanceSquared < Distance[InCandidate])
			{
				Distance[InCandidate] = DistanceSquared;
				Heap.KeyDecreased(InCandidate);
			}
		};

		while (Order.Num() < TileNumPoints)
		{
			// The farthest point from every pick so far; ties go to the lower pool index.
			const int32 Pick = Heap.Pop();
			const int64 Radius = static_cast<int64>(FMath::Sqrt(static_cast<double>(Distance[Pick]))) + 1;
			Order.Add(Pick);
			Taken[Pick] = 1;

			// Only points within the current coverage radius can be closer to this pick than to their own nearest.
			const int32 Reach = static_cast<int32>(Radius / PoolCellSize) + 1;
			if (2 * Reach + 1 >= PoolGridSide)
			{
				for (int32 q = 0; q < PoolSize; q++)
				{
					Relax(Pick, q);
				}
				continue;
			}

			const int32 CX = PoolX[Pick] / PoolCellSize;
			const int32 CY = PoolY[Pick] / PoolCellSize;
			for (int32 OY = -Reach; OY <= Reach; OY++)
			{
				const int32 Row = ((CY + OY + PoolGridSide) % PoolGridSide) * PoolGridSide;
				for (int32 OX = -Reach; OX <= Reach; OX++)
				{
					const int32 Cell = Row + (CX + OX + PoolGridSide) % PoolGridSide;
					for (int32 k = CellStart[Cell]; k < CellStart[Cell + 1]; k++)
					{
						Relax(Pick, CellItems[k]);
					}
				}
			}
		}

		for (int32 Rank = 0; Rank < TileNumPoints; Rank++)
		{
			OutTileXY[Rank * 2] = static_cast<uint16>(PoolX[Order[Rank]]);
			OutTileXY[Rank * 2 + 1] = static_cast<uint16>(PoolY[Order[Rank]]);
		}
	}

	void BuildMask(const TArrayView<uint16> OutMask)
	{
		using namespace Build;
		check(OutMask.Num() == MaskCells);

		TArray<uint8> Pattern;
		Pattern.Init(0, MaskCells);
		TArray<int64> Energy;
		Energy.Init(0, MaskCells);

		FSplitMix64 Random(MaskSeed);
		for (int32 Placed = 0; Placed < MaskInitialOnes;)
		{
			const int32 Cell = Random.Below(MaskCells);
			if (!Pattern[Cell])
			{
				Pattern[Cell] = 1;
				ApplyKernel(Energy, Cell, 1);
				Placed++;
			}
		}

		// Relax the initial pattern: move the tightest cluster into the largest void until they coincide.
		for (int32 Iteration = 0; Iteration < 100000; Iteration++)
		{
			const int32 Tightest = FindTightestCluster(Pattern, Energy);
			Pattern[Tightest] = 0;
			ApplyKernel(Energy, Tightest, -1);

			const int32 Void = FindLargestVoid(Pattern, Energy);
			Pattern[Void] = 1;
			ApplyKernel(Energy, Void, 1);

			if (Void == Tightest)
			{
				break;
			}
		}

		// Ranks below the initial count: strip the tightest clusters of a copy, last rank first.
		{
			TArray<uint8> PhasePattern(Pattern);
			TArray<int64> PhaseEnergy(Energy);
			for (int32 Rank = MaskInitialOnes - 1; Rank >= 0; Rank--)
			{
				const int32 Tightest = FindTightestCluster(PhasePattern, PhaseEnergy);
				OutMask[Tightest] = static_cast<uint16>(Rank);
				PhasePattern[Tightest] = 0;
				ApplyKernel(PhaseEnergy, Tightest, -1);
			}
		}

		// The rest fill the largest voids. The least-energy zero is also the tightest cluster of zeros, so one rule covers
		// both of Ulichney's later phases.
		for (int32 Rank = MaskInitialOnes; Rank < MaskCells; Rank++)
		{
			const int32 Void = FindLargestVoid(Pattern, Energy);
			OutMask[Void] = static_cast<uint16>(Rank);
			Pattern[Void] = 1;
			ApplyKernel(Energy, Void, 1);
		}
	}

	TConstArrayView<uint16> GetTileXY()
	{
		Build::EnsureBuilt();
		return MakeArrayView(Build::TileXY);
	}

	TConstArrayView<uint16> GetDitherMask()
	{
		Build::EnsureBuilt();
		return MakeArrayView(Build::DitherMask);
	}

	void PrebuildTables()
	{
		Build::PrebuildTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []() { Build::EnsureBuilt(); }, UE::Tasks::ETaskPriority::BackgroundNormal);
	}

	void WaitForPrebuild()
	{
		if (Build::PrebuildTask.IsValid())
		{
			Build::PrebuildTask.Wait();
			Build::PrebuildTask = UE::Tasks::FTask();
		}
	}
}
