// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "Misc/Char.h"
#include "Misc/StringBuilder.h"
#include "UObject/SoftObjectPath.h"

/**
 * Persistable value hashing: the same values hash the same on every session, platform, build and engine
 * version, so a result can be saved and compared later. Nothing here goes through GetTypeHash, whose results
 * TypeHash.h says must not leave the running process (FName hashes are per-process ids).
 */
namespace PCGExHashHelpers
{
	/** FNV-1a 64-bit offset basis; start every hash here. */
	constexpr uint64 StableSeed = 14695981039346656037ULL;
	constexpr uint64 StablePrime = 1099511628211ULL;

	/** Word-wise rather than byte-wise, so byte order never enters the result. */
	FORCEINLINE uint64 MixWord(const uint64 InHash, const uint64 InWord)
	{
		return (InHash ^ InWord) * StablePrime;
	}

	/** Exact bits: values that differ in any bit hash apart. */
	FORCEINLINE uint64 MixFloat(const uint64 InHash, const float InValue)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &InValue, sizeof(Bits));
		return MixWord(InHash, Bits);
	}

	/** Exact bits: values that differ in any bit hash apart. */
	FORCEINLINE uint64 MixDouble(const uint64 InHash, const double InValue)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &InValue, sizeof(Bits));
		return MixWord(InHash, Bits);
	}

	/** ASCII case folding, like FString and FName equality; hashed as UTF-8 so TCHAR width never enters the result. */
	inline uint64 MixString(uint64 InHash, const FStringView InValue)
	{
		TStringBuilder<256> Folded;
		for (const TCHAR Char : InValue)
		{
			Folded.AppendChar(FChar::ToLower(Char));
		}

		const FTCHARToUTF8 Utf8(Folded.GetData(), Folded.Len());
		InHash = MixWord(InHash, static_cast<uint64>(Utf8.Length()));
		for (int32 i = 0; i < Utf8.Length(); i++)
		{
			InHash = MixWord(InHash, static_cast<uint8>(Utf8.Get()[i]));
		}
		return InHash;
	}

	/** Mixes one value of any basic PCG metadata type. The caller mixes the type itself when types must stay apart. */
	template <typename T>
	uint64 MixValue(const uint64 InHash, const T& InValue)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return MixWord(InHash, InValue ? 1ULL : 0ULL);
		}
		else if constexpr (std::is_same_v<T, int32> || std::is_same_v<T, int64>)
		{
			return MixWord(InHash, static_cast<uint64>(static_cast<int64>(InValue)));
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return MixFloat(InHash, InValue);
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			return MixDouble(InHash, InValue);
		}
		else if constexpr (std::is_same_v<T, FVector2D>)
		{
			return MixDouble(MixDouble(InHash, InValue.X), InValue.Y);
		}
		else if constexpr (std::is_same_v<T, FVector>)
		{
			return MixDouble(MixDouble(MixDouble(InHash, InValue.X), InValue.Y), InValue.Z);
		}
		else if constexpr (std::is_same_v<T, FVector4> || std::is_same_v<T, FQuat>)
		{
			return MixDouble(MixDouble(MixDouble(MixDouble(InHash, InValue.X), InValue.Y), InValue.Z), InValue.W);
		}
		else if constexpr (std::is_same_v<T, FRotator>)
		{
			return MixDouble(MixDouble(MixDouble(InHash, InValue.Pitch), InValue.Yaw), InValue.Roll);
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return MixValue(MixValue(MixValue(InHash, InValue.GetRotation()), InValue.GetTranslation()), InValue.GetScale3D());
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return MixString(InHash, InValue);
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return MixString(InHash, InValue.ToString());
		}
		else
		{
			static_assert(sizeof(T) == 0, "PCGExHashHelpers::MixValue: unsupported type.");
			return InHash;
		}
	}
}
