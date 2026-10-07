// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"

namespace PCGExArrayHelpers
{
	PCGEXCORE_API TArray<FString> GetStringArrayFromCommaSeparatedList(const FString& InCommaSeparatedString);

	PCGEXCORE_API void AppendEntriesFromCommaSeparatedList(const FString& InCommaSeparatedString, TSet<FString>& OutStrings);

	PCGEXCORE_API void AppendUniqueEntriesFromCommaSeparatedList(const FString& InCommaSeparatedString, TArray<FString>& OutStrings);

	template <typename T>
	static void Reverse(TArrayView<T> View)
	{
		const int32 Count = View.Num();
		for (int32 i = 0; i < Count / 2; ++i)
		{
			Swap(View[i], View[Count - 1 - i]);
		}
	}

	template <typename T>
	static void InitArray(TArray<T>& InArray, const int32 Num)
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			InArray.SetNumUninitialized(Num);
		}
		else
		{
			InArray.SetNum(Num);
		}
	}

	template <typename T>
	static void InitArray(TSharedPtr<TArray<T>>& InArray, const int32 Num)
	{
		if (!InArray)
		{
			InArray = MakeShared<TArray<T>>();
		}
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			InArray->SetNumUninitialized(Num);
		}
		else
		{
			InArray->SetNum(Num);
		}
	}

	template <typename T>
	static void InitArray(TSharedRef<TArray<T>> InArray, const int32 Num)
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			InArray.SetNumUninitialized(Num);
		}
		else
		{
			InArray.SetNum(Num);
		}
	}

	template <typename T>
	static void InitArray(TArray<T>* InArray, const int32 Num)
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			InArray->SetNumUninitialized(Num);
		}
		else
		{
			InArray->SetNum(Num);
		}
	}

	template <typename T>
	void ReorderArray(TArray<T>& InArray, const TArray<int32>& InOrder)
	{
		const int32 NumElements = InOrder.Num();
		check(NumElements <= InArray.Num());

		TBitArray<> Visited;
		Visited.Init(false, NumElements);

		for (int32 i = 0; i < NumElements; ++i)
		{
			if (Visited[i])
			{
				continue;
			}

			int32 Current = i;
			int32 Next = InOrder[Current];

			if (Next == Current)
			{
				Visited[Current] = true;
				continue;
			}

			T Temp = MoveTemp(InArray[Current]);

			while (!Visited[Next])
			{
				InArray[Current] = MoveTemp(InArray[Next]);

				Visited[Current] = true;
				Current = Next;
				Next = InOrder[Current];
			}

			InArray[Current] = MoveTemp(Temp);
			Visited[Current] = true;
		}
	}

	template <typename T>
	static void ShiftArrayToSmallest(TArray<T>& InArray)
	{
		const int32 Num = InArray.Num();
		if (Num <= 1)
		{
			return;
		}

		int32 MinIndex = 0;
		for (int32 i = 1; i < Num; ++i)
		{
			if (InArray[i] < InArray[MinIndex])
			{
				MinIndex = i;
			}
		}

		if (MinIndex > 0)
		{
			TArray<T> TempArray;
			TempArray.Append(InArray.GetData() + MinIndex, Num - MinIndex);
			TempArray.Append(InArray.GetData(), MinIndex);

			FMemory::Memcpy(InArray.GetData(), TempArray.GetData(), sizeof(T) * Num);
		}
	}

	PCGEXCORE_API void ArrayOfIndices(TArray<int32>& OutArray, const int32 InNum, const int32 Offset = 0);
	PCGEXCORE_API int32 ArrayOfIndices(TArray<int32>& OutArray, const TArrayView<const int8>& Mask, const int32 Offset, const bool bInvert = false);
	PCGEXCORE_API int32 ArrayOfIndices(TArray<int32>& OutArray, const TBitArray<>& Mask, const int32 Offset, const bool bInvert = false);
}
