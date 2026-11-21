// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExArrayHelpers.h"

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

namespace PCGExArrayHelpers
{
	TArray<FString> GetStringArrayFromCommaSeparatedList(const FString& InCommaSeparatedString)
	{
		TArray<FString> Result;
		InCommaSeparatedString.ParseIntoArray(Result, TEXT(","));
		// Trim leading and trailing spaces
		for (int i = 0; i < Result.Num(); i++)
		{
			FString& String = Result[i];
			String.TrimStartAndEndInline();
			if (String.IsEmpty())
			{
				Result.RemoveAt(i);
				i--;
			}
		}

		return Result;
	}

	void AppendEntriesFromCommaSeparatedList(const FString& InCommaSeparatedString, TSet<FString>& OutStrings)
	{
		if (InCommaSeparatedString.IsEmpty())
		{
			return;
		}

		for (const FString& String : GetStringArrayFromCommaSeparatedList(InCommaSeparatedString))
		{
			OutStrings.Add(String);
		}
	}

	void AppendUniqueEntriesFromCommaSeparatedList(const FString& InCommaSeparatedString, TArray<FString>& OutStrings)
	{
		if (InCommaSeparatedString.IsEmpty())
		{
			return;
		}

		for (const FString& String : GetStringArrayFromCommaSeparatedList(InCommaSeparatedString))
		{
			OutStrings.AddUnique(String);
		}
	}

	void ArrayOfIndices(TArray<int32>& OutArray, const int32 InNum, const int32 Offset)
	{
		OutArray.Reserve(InNum);
		for (int i = 0; i < InNum; i++)
		{
			OutArray.Add(Offset + i);
		}
	}

	int32 ArrayOfIndices(TArray<int32>& OutArray, const TArrayView<const int8>& Mask, const int32 Offset, const bool bInvert)
	{
		const int32 InNum = Mask.Num();

		OutArray.Reset();
		OutArray.Reserve(InNum);

		if (bInvert)
		{
			for (int i = 0; i < InNum; i++)
			{
				if (!Mask[i])
				{
					OutArray.Add(Offset + i);
				}
			}
		}
		else
		{
			for (int i = 0; i < InNum; i++)
			{
				if (Mask[i])
				{
					OutArray.Add(Offset + i);
				}
			}
		}

		OutArray.Shrink();
		return OutArray.Num();
	}

	int32 ArrayOfIndices(TArray<int32>& OutArray, const TBitArray<>& Mask, const int32 Offset, const bool bInvert)
	{
		const int32 InNum = Mask.Num();

		OutArray.Reset();
		OutArray.Reserve(InNum);

		if (bInvert)
		{
			for (int i = 0; i < InNum; i++)
			{
				if (!Mask[i])
				{
					OutArray.Add(Offset + i);
				}
			}
		}
		else
		{
			for (int i = 0; i < InNum; i++)
			{
				if (Mask[i])
				{
					OutArray.Add(Offset + i);
				}
			}
		}

		OutArray.Shrink();
		return OutArray.Num();
	}
}
