// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMathMean.generated.h"

UENUM(BlueprintType)
enum class EPCGExMeanMeasure : uint8
{
	Relative = 0 UMETA(DisplayName = "Relative", ToolTip="Input value will be normalized between 0..1, or used as a factor. (what it means exactly depends on context. See node-specific documentation.)"),
	Discrete = 1 UMETA(DisplayName = "Discrete", ToolTip="Raw value will be used, or used as absolute. (what it means exactly depends on context. See node-specific documentation.)"),
};

UENUM(BlueprintType)
enum class EPCGExMeanMethod : uint8
{
	Average = 0 UMETA(DisplayName = "Average", ToolTip="Average"),
	Median  = 1 UMETA(DisplayName = "Median", ToolTip="Median"),
	ModeMin = 2 UMETA(DisplayName = "Mode (Highest)", ToolTip="Most common value; ties resolve to the highest one"),
	ModeMax = 3 UMETA(DisplayName = "Mode (Lowest)", ToolTip="Most common value; ties resolve to the lowest one"),
	Central = 4 UMETA(DisplayName = "Central", ToolTip="Central uses the middle value between Min/Max input values."),
	Fixed   = 5 UMETA(DisplayName = "Fixed", ToolTip="Fixed threshold"),
};

namespace PCGExMath
{
	template <typename T>
	FORCEINLINE static T GetAverage(const TArray<T>& Values)
	{
		T Sum = 0;
		for (const T Value : Values)
		{
			Sum += Value;
		}
		return Sum / Values.Num();
	}

	// Quickselect partition helper - O(n) average for finding k-th element
	template <typename T>
	static int32 QuickSelectPartition(TArray<T>& Arr, int32 Left, int32 Right, int32 PivotIndex)
	{
		const T PivotValue = Arr[PivotIndex];
		Swap(Arr[PivotIndex], Arr[Right]); // Move pivot to end
		int32 StoreIndex = Left;
		for (int32 i = Left; i < Right; i++)
		{
			if (Arr[i] < PivotValue)
			{
				Swap(Arr[StoreIndex], Arr[i]);
				StoreIndex++;
			}
		}
		Swap(Arr[Right], Arr[StoreIndex]); // Move pivot to final place
		return StoreIndex;
	}

	// Quickselect - O(n) average case for finding k-th smallest element
	template <typename T>
	static T QuickSelect(TArray<T>& Arr, int32 Left, int32 Right, int32 K)
	{
		while (Left < Right)
		{
			// Use middle element as pivot for better average performance
			const int32 PivotIndex = Left + (Right - Left) / 2;
			const int32 NewPivotIndex = QuickSelectPartition(Arr, Left, Right, PivotIndex);

			if (NewPivotIndex == K)
			{
				return Arr[K];
			}
			if (K < NewPivotIndex)
			{
				Right = NewPivotIndex - 1;
			}
			else
			{
				Left = NewPivotIndex + 1;
			}
		}
		return Arr[Left];
	}

	template <typename T>
	FORCEINLINE static T GetMedian(const TArray<T>& Values)
	{
		if (Values.IsEmpty())
		{
			return T{};
		}
		if (Values.Num() == 1)
		{
			return Values[0];
		}

		// Copy for in-place quickselect (avoids full sort - O(n) avg vs O(n log n))
		TArray<T> WorkingCopy;
		WorkingCopy.Reserve(Values.Num());
		WorkingCopy.Append(Values);

		const int32 N = WorkingCopy.Num();
		const int32 Mid = N / 2;

		if (N % 2 == 1)
		{
			// Odd count: single middle element
			return QuickSelect(WorkingCopy, 0, N - 1, Mid);
		}
		// Even count: average of two middle elements
		const T Lower = QuickSelect(WorkingCopy, 0, N - 1, Mid - 1);
		const T Upper = QuickSelect(WorkingCopy, 0, N - 1, Mid);
		return (Lower + Upper) / 2;
	}

	PCGEXCORE_API double GetMode(const TArray<double>& Values, const bool bHighest, const uint32 Tolerance = 5);
}
