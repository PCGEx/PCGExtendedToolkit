// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"

namespace PCGExHelpers
{
	/**
	 * Tells whether FProperty and UFunction pointers looked up on a set of structs are still the ones they own.
	 * Compiling a Blueprint or a user struct destroys and rebuilds its fields while the UStruct object stays the
	 * same, so whoever caches such pointers across ticks takes a stamp at lookup time and checks it before each use.
	 * A cooked struct never rebuilds its fields: without editor-only data the stamp is empty and always current.
	 */
	struct FStructLayoutStamp
	{
		/** Stamps InStruct and its super structs: a field found on a struct can be owned by any of them. */
		void Add(const UStruct* InStruct)
		{
#if WITH_EDITORONLY_DATA
			// Stops at the first native class: it never rebuilds its fields, and neither does anything above it.
			for (const UStruct* Struct = InStruct; Struct && !Struct->IsNative(); Struct = Struct->GetSuperStruct())
			{
				const TWeakObjectPtr<const UStruct> WeakStruct(Struct);
				if (!Entries.ContainsByPredicate([&WeakStruct](const FEntry& Entry) { return Entry.Struct == WeakStruct; }))
				{
					Entries.Add(FEntry{WeakStruct, Struct->FieldPathSerialNumber});
				}
			}
#endif
		}

		bool IsCurrent() const
		{
#if WITH_EDITORONLY_DATA
			for (const FEntry& Entry : Entries)
			{
				// UStruct::FieldPathSerialNumber changes every time the struct's properties get destroyed.
				const UStruct* Struct = Entry.Struct.Get();
				if (!Struct || Struct->FieldPathSerialNumber != Entry.Serial)
				{
					return false;
				}
			}
#endif
			return true;
		}

#if WITH_EDITORONLY_DATA

	private:
		struct FEntry
		{
			TWeakObjectPtr<const UStruct> Struct;
			int32 Serial = 0;
		};

		TArray<FEntry, TInlineAllocator<4>> Entries;
#endif
	};
}
