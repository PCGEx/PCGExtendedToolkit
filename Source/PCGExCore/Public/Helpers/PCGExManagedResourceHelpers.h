// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "PCGCrc.h"

namespace PCGExManagedHelpers
{
	/** Find the first managed resource of type T matching CRC + validator. Mark as reused if found. */
	template <typename T>
	T* TryReuseManagedResource(UPCGComponent* Component, const FPCGCrc& Crc, TFunctionRef<bool(const T*)> Validator)
	{
		if (!Component || !Crc.IsValid()) { return nullptr; }

		T* Found = nullptr;
		Component->ForEachManagedResource(
			[&](UPCGManagedResource* InResource)
			{
				if (Found) { return; }

				T* Typed = Cast<T>(InResource);
				if (!Typed) { return; }

				if (!Typed->GetCrc().IsValid() || !(Typed->GetCrc() == Crc)) { return; }

				if (!Validator(Typed)) { return; }

				Typed->MarkAsReused();
				Found = Typed;
			});

		return Found;
	}

	/** Overload without validator (CRC-only match). */
	template <typename T>
	T* TryReuseManagedResource(UPCGComponent* Component, const FPCGCrc& Crc)
	{
		return TryReuseManagedResource<T>(Component, Crc, [](const T*) { return true; });
	}

	/** Find ALL managed resources of type T matching CRC. Mark all as reused if count == ExpectedCount. */
	template <typename T>
	bool TryReuseAllManagedResources(UPCGComponent* Component, const FPCGCrc& Crc, int32 ExpectedCount)
	{
		if (!Component || !Crc.IsValid() || ExpectedCount <= 0) { return false; }

		TArray<T*> Matched;
		Component->ForEachManagedResource(
			[&](UPCGManagedResource* InResource)
			{
				T* Typed = Cast<T>(InResource);
				if (!Typed) { return; }

				if (!Typed->GetCrc().IsValid() || !(Typed->GetCrc() == Crc)) { return; }

				Matched.Add(Typed);
			});

		if (Matched.Num() != ExpectedCount) { return false; }

		for (T* Resource : Matched)
		{
			Resource->MarkAsReused();
		}

		return true;
	}
}
