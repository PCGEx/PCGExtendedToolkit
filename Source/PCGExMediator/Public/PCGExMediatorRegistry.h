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

	/** Alive for the lifetime of the registration (FPCGExMediatorDomain::AddFormat owns it for you). */
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

/** Decoded value of a member earlier in binding order, not yet applied; null when the document did not carry it. */
using FPCGExMediatorDecodedMember = TFunctionRef<const void*(FName Member)>;

/**
 * How an object is authored through the mediator: which of its members travel, in what order, and how to
 * wrap a member's decode when the domain needs context around it (e.g. a schema scope while rows decode).
 * Every member decodes into scratch first; then all are applied in order through the host's own edit hooks.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorBinding
{
	const UClass* HostClass = nullptr;

	/** Export and import order. Each must be a UPROPERTY of HostClass (or a parent). */
	TArray<FName> Members;

	/** Optional. Must invoke Import exactly once; Decoded gives the scratch value of an earlier member (null = the live one stands). */
	TFunction<void(UObject* Host, FName Member, FPCGExMediatorDecodedMember Decoded, TFunctionRef<void()> Import)> WrapImport;

	/** Optional. Runs after every member applied, inside the import transaction, for host work no edit hook covers. */
	TFunction<void(UObject* Host)> PostImport;

	FString Summary;

	bool IsValid() const
	{
		return HostClass != nullptr && !Members.IsEmpty();
	}
};

/** Game-thread only (asserted); registrations happen in StartupModule and are removed in ShutdownModule. */
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

/**
 * A domain's registrations, released together: AddFormat / AddBinding from StartupModule, Reset from
 * ShutdownModule (by key only, so it is safe after the UObjects are gone). Owns the converters it creates.
 */
class PCGEXMEDIATOR_API FPCGExMediatorDomain
{
public:
	FPCGExMediatorDomain() = default;
	~FPCGExMediatorDomain();

	FPCGExMediatorDomain(const FPCGExMediatorDomain&) = delete;
	FPCGExMediatorDomain& operator=(const FPCGExMediatorDomain&) = delete;

	/** Constructs the converter in place and registers the format; the domain keeps the converter alive. */
	template <typename TConverter, typename... TArgs>
	TConverter& AddFormat(const FName Id, const int32 Version, const UScriptStruct* Struct, TFunction<TSharedPtr<FJsonObject>()> Describe, FString Summary, TArgs&&... Args)
	{
		TSharedRef<TConverter> Converter = MakeShared<TConverter>(Forward<TArgs>(Args)...);
		FPCGExMediatorFormat Format;
		Format.Id = Id;
		Format.Version = Version;
		Format.Struct = Struct;
		Format.Converter = &Converter.Get();
		Format.Describe = MoveTemp(Describe);
		Format.Summary = MoveTemp(Summary);
		AddFormat(Format, Converter);
		return Converter.Get();
	}

	/** Registers a format whose converter Owner keeps alive (or the caller does, when Owner is null). */
	void AddFormat(const FPCGExMediatorFormat& Format, const TSharedPtr<const IJsonObjectStructConverter>& Owner = nullptr);
	void AddBinding(const FPCGExMediatorBinding& Binding);

	bool HasFormatForStruct(const UScriptStruct* Struct) const;

	/** Unregisters everything in reverse order of registration. */
	void Reset();

private:
	TArray<FName> FormatIds;
	TArray<const UScriptStruct*> FormatStructs;
	TArray<const UClass*> BindingClasses;
	TArray<TSharedPtr<const IJsonObjectStructConverter>> OwnedConverters;
};
