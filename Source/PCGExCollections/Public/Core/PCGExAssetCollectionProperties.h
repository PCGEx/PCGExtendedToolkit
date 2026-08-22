// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExProperty.h"
#include "Core/PCGExAssetCollection.h"

// Definitions for FPCGExAssetCollectionEntry's property templates. They live here, not in
// PCGExAssetCollection.h, because the FPCGExProperty::TryGetValue call is non-dependent: leaving it
// there would require the property base to be complete in every TU that parses the collection header.
// Include this from translation units that actually resolve entry properties.

template <typename T>
bool FPCGExAssetCollectionEntry::TryGetPropertyValue(const UPCGExAssetCollection* OwningCollection, FName PropertyName, T& Out) const
{
	if (const FPCGExProperty* Base = GetResolvedPropertyBase(OwningCollection, PropertyName))
	{
		return Base->TryGetValue(Out);
	}
	return false;
}

template <typename T>
const T* FPCGExAssetCollectionEntry::GetResolvedProperty(const UPCGExAssetCollection* OwningCollection, FName PropertyName) const
{
	static_assert(TIsDerivedFrom<T, FPCGExProperty>::Value, "T must derive from FPCGExProperty");

	const FInstancedStruct* Slot = ResolvePropertySlot(OwningCollection, PropertyName, T::StaticStruct());
	return Slot ? Slot->GetPtr<T>() : nullptr;
}
