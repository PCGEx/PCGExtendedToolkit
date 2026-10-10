// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct IJsonObjectStructConverter;

/**
 * One JSON document kind: a struct, the converter that owns its JSON shape, and its self-description.
 * Registering a format also registers the converter with the engine's FJsonObjectStructInterfaceRegistry, so
 * the shape applies wherever the struct appears -- nested in any host, and through Epic's toolset library.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorFormat
{
	/** Envelope id, e.g. "pcgex.property-schema". */
	FName Id = NAME_None;

	/** Document version this converter writes and the highest it reads. */
	int32 Version = 1;

	const UScriptStruct* Struct = nullptr;

	/** Owned by the registering module for the lifetime of the registration (a function-local static is fine). */
	const IJsonObjectStructConverter* Converter = nullptr;

	/** JSON Schema of the document body (the envelope's "data"). */
	TFunction<TSharedPtr<FJsonObject>()> Describe;

	/** One line for listings. */
	FString Summary;

	bool IsValid() const
	{
		return !Id.IsNone() && Struct != nullptr && Converter != nullptr;
	}
};

/**
 * How an object is authored through the mediator: which of its members travel, in what order, and how to
 * wrap a member's import when the domain needs context around it (e.g. a schema scope while rows decode).
 * After an import the transport drives the host's own PostEditChangeProperty for each written member.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorBinding
{
	const UClass* HostClass = nullptr;

	/** Export and import order. Each must be a UPROPERTY of HostClass (or a parent). */
	TArray<FName> Members;

	/** Optional. Must invoke Import exactly once; whatever it sets up stays alive across that call. */
	TFunction<void(UObject* Host, FName Member, TFunctionRef<void()> Import)> WrapImport;

	FString Summary;

	bool IsValid() const
	{
		return HostClass != nullptr && !Members.IsEmpty();
	}
};

/** Game-thread only; registrations happen in StartupModule and are removed in ShutdownModule. */
class PCGEXMEDIATOR_API FPCGExMediatorRegistry
{
public:
	static void RegisterFormat(const FPCGExMediatorFormat& InFormat);
	static void UnregisterFormat(FName InId);

	static TSharedPtr<const FPCGExMediatorFormat> FindFormat(FName InId);
	static TSharedPtr<const FPCGExMediatorFormat> FindFormatForStruct(const UScriptStruct* InStruct);
	static void GetFormats(TArray<TSharedPtr<const FPCGExMediatorFormat>>& OutFormats);

	static void RegisterBinding(const FPCGExMediatorBinding& InBinding);
	static void UnregisterBinding(const UClass* InHostClass);

	/** Walks up the class chain, so a binding on a base class serves its subclasses. */
	static TSharedPtr<const FPCGExMediatorBinding> FindBinding(const UClass* InHostClass);
	static void GetBindings(TArray<TSharedPtr<const FPCGExMediatorBinding>>& OutBindings);
};
