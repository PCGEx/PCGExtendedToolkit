// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGComponent.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"

namespace PCGExManagedHelpers
{
	/** Find the first managed resource of type T matching CRC + validator. Mark as reused if found. */
	template <typename T>
	T* TryReuseManagedResource(UPCGComponent* Component, const FPCGCrc& Crc, TFunctionRef<bool(const T*)> Validator)
	{
		if (!Component || !Crc.IsValid())
		{
			return nullptr;
		}

		T* Found = nullptr;
		Component->ForEachManagedResource(
			[&](UPCGManagedResource* InResource)
			{
				if (Found)
				{
					return;
				}

				T* Typed = Cast<T>(InResource);
				if (!Typed)
				{
					return;
				}

				if (!Typed->GetCrc().IsValid() || !(Typed->GetCrc() == Crc))
				{
					return;
				}

				if (!Validator(Typed))
				{
					return;
				}

				Typed->MarkAsReused();
				Found = Typed;
			});

		return Found;
	}

	/** Overload without validator (CRC-only match). */
	template <typename T>
	T* TryReuseManagedResource(UPCGComponent* Component, const FPCGCrc& Crc)
	{
		return TryReuseManagedResource<T>(Component, Crc, [](const T*)
		{
			return true;
		});
	}
}
