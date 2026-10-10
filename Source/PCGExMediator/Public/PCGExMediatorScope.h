// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * RAII entry on a per-thread context stack. TStorage names the stack: `using FValue = ...;` plus an EXPORTED
 * `static TArray<FValue>& Get()`, so every module that pushes or reads the stack shares one copy (a header-local
 * static would give each DLL its own).
 */
template <typename TStorage>
class TPCGExMediatorScope
{
public:
	using FValue = typename TStorage::FValue;

	explicit TPCGExMediatorScope(FValue InValue)
	{
		TStorage::Get().Add(MoveTemp(InValue));
	}

	~TPCGExMediatorScope()
	{
		TArray<FValue>& Stack = TStorage::Get();
		Stack.RemoveAt(Stack.Num() - 1, EAllowShrinking::No);
	}

	TPCGExMediatorScope(const TPCGExMediatorScope&) = delete;
	TPCGExMediatorScope& operator=(const TPCGExMediatorScope&) = delete;

	/** Innermost entry, or null outside any scope. */
	static const FValue* Current()
	{
		const TArray<FValue>& Stack = TStorage::Get();
		return Stack.IsEmpty() ? nullptr : &Stack.Last();
	}

	static bool IsActive()
	{
		return !TStorage::Get().IsEmpty();
	}
};
