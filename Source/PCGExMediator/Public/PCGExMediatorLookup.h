// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

/** Object and type resolution for every codec in the layer; the one place that loads. */
namespace PCGExMediator
{
	/** An object by path (plain or Class'/Path' form), loaded through LoadBlocking_AnyThread when needed. Null for an empty or unresolvable path. */
	PCGEXMEDIATOR_API UObject* ResolveObject(const FString& Path);

	/** A reflected type by path, short name or prefixed C++ name (U / A / F / E), native first; in-memory only. */
	PCGEXMEDIATOR_API UField* FindTypeByName(UClass* TypeClass, const FString& NameOrPath);

	template <typename T>
	T* FindType(const FString& NameOrPath)
	{
		return Cast<T>(FindTypeByName(T::StaticClass(), NameOrPath));
	}

	/** FindType, then the asset-backed kind by path (Blueprint class, user-defined enum or struct). */
	template <typename T>
	T* LoadType(const FString& NameOrPath)
	{
		if (T* Found = FindType<T>(NameOrPath)) { return Found; }
		return Cast<T>(ResolveObject(NameOrPath));
	}
}
