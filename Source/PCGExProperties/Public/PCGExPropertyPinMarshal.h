// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FPCGExProperty;
class FProperty;

/**
 * Shared marshalling between FPCGExProperty values and Blueprint pin memory, used by the
 * wildcard CustomThunk function libraries (UPCGExPropertyBlueprintLibrary,
 * UPCGExCollectionEntryBlueprintLibrary). Component/collection lookup stays with the
 * individual libraries; only the FProperty-driven value transport lives here.
 */
namespace PCGExPropertyPinMarshal
{
	/**
	 * Write a property's value into a Blueprint pin's memory, dispatching on the pin's
	 * FProperty type via PCGPropertyHelpers::GetMetadataTypeFromProperty. Pin types that
	 * don't map to an EPCGMetadataTypes entry (hard object/class refs) fall back to
	 * TryWriteToObjectPin. Returns false on type mismatch.
	 */
	PCGEXPROPERTIES_API bool TryWriteToPin(const FPCGExProperty* Prop, const FProperty* OutProp, void* OutMem);

	/**
	 * Read a Blueprint pin's memory into a property's value. Inverse of TryWriteToPin,
	 * with the same hard object/class ref fallback through TryReadFromObjectPin.
	 */
	PCGEXPROPERTIES_API bool TryReadFromPin(FPCGExProperty* Prop, const FProperty* InProp, const void* InMem);

	/**
	 * Fallback path for Blueprint pins that PCG's helper doesn't map to an EPCGMetadataTypes
	 * entry: hard object / class references. Treats the underlying property as a soft path,
	 * resolves it (non-blocking when already loaded, blocking sync load otherwise) and writes
	 * the typed pointer back into the pin's memory. Returns false on type mismatch.
	 */
	PCGEXPROPERTIES_API bool TryWriteToObjectPin(const FPCGExProperty* Prop, const FProperty* OutProp, void* OutMem);

	/**
	 * Fallback path for the inverse direction: a hard object/class ref input is read as a
	 * soft path and forwarded to the property's TryReadValue.
	 */
	PCGEXPROPERTIES_API bool TryReadFromObjectPin(FPCGExProperty* Prop, const FProperty* InProp, const void* InMem);

	/** Resolves SoftPath, loading it on a miss (game thread only); null when it is not an ExpectedClass. A null ExpectedClass accepts everything. */
	PCGEXPROPERTIES_API UObject* ResolveObjectFromPath(const FSoftObjectPath& SoftPath, const UClass* ExpectedClass);

	/** Class flavour of ResolveObjectFromPath: null unless the class derives from ExpectedClass. */
	PCGEXPROPERTIES_API UClass* ResolveClassFromPath(const FSoftClassPath& SoftPath, const UClass* ExpectedClass);
}
