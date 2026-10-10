// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

class FJsonObject;
class FJsonValue;
struct FPCGExNumericRange;
struct FPCGExProperty;

/**
 * How one FPCGExProperty type travels as JSON. Value hooks carry the authored value (a schema entry's "default",
 * an override's value); structural hooks carry schema-owned fields on the entry object (enum class, allowed
 * class, range). Diagnostics go through PCGExMediator::Report at the current path. The hooks a type does not
 * provide are simply skipped.
 *
 * Registered per UScriptStruct from a module's StartupModule; a type with no registration falls back to the
 * output-type hooks when its GetOutputType() has a dialect shape (the 15 legacy types and Color).
 */
struct PCGEXPROPERTIESEDITOR_API FPCGExPropertyMediatorHooks
{
	TFunction<TSharedPtr<FJsonValue>(const FPCGExProperty& Property)> EncodeValue;
	TFunction<bool(FPCGExProperty& Property, const TSharedPtr<FJsonValue>& Json)> DecodeValue;

	TFunction<void(const FPCGExProperty& Property, FJsonObject& Entry)> EncodeStructural;
	TFunction<bool(FPCGExProperty& Property, const FJsonObject& Entry)> DecodeStructural;

	/** JSON Schema fragment for the value; Prototype is an instance of the type (may be null). */
	TFunction<TSharedPtr<FJsonObject>(const FPCGExProperty* Prototype)> DescribeValue;

	/** Adds the structural keys' schemas to an entry's "properties" object. */
	TFunction<void(FJsonObject& EntryProperties)> DescribeStructural;

	bool HasValue() const
	{
		return static_cast<bool>(EncodeValue) && static_cast<bool>(DecodeValue);
	}
};

namespace PCGExPropertyMediator
{
	PCGEXPROPERTIESEDITOR_API void RegisterHooks(const UScriptStruct* Struct, const FPCGExPropertyMediatorHooks& Hooks);
	PCGEXPROPERTIESEDITOR_API void UnregisterHooks(const UScriptStruct* Struct);

	/** Exact registration, else the output-type default when the type's output has a dialect shape, else null. */
	PCGEXPROPERTIESEDITOR_API const FPCGExPropertyMediatorHooks* FindHooks(const UScriptStruct* Struct);

	PCGEXPROPERTIESEDITOR_API bool HasValueSupport(const UScriptStruct* Struct);

	/** Catalog types with no value hooks. Empty is the expected state; a test keeps it that way. */
	PCGEXPROPERTIESEDITOR_API void GetTypesWithoutValueSupport(TArray<FName>& OutTypeNames);

	/** The default: value through TryWriteValue / TryReadValue at the property's output type. */
	PCGEXPROPERTIESEDITOR_API FPCGExPropertyMediatorHooks MakeOutputTypeHooks();

	/**
	 * Schema a decoding override set syncs to when its target is not schema-parallel yet (fresh rows). Pushed by
	 * the binding that knows the host; nested scopes stack, the innermost wins.
	 */
	class PCGEXPROPERTIESEDITOR_API FOverridesSchemaScope
	{
	public:
		explicit FOverridesSchemaScope(TArray<FInstancedStruct> InSchema);
		~FOverridesSchemaScope();

		FOverridesSchemaScope(const FOverridesSchemaScope&) = delete;
		FOverridesSchemaScope& operator=(const FOverridesSchemaScope&) = delete;

		static const TArray<FInstancedStruct>* Current();

	private:
		TArray<FInstancedStruct> Schema;
	};

	/** Numeric range as { "min", "max", "clampMin", "clampMax" }; null when nothing is clamped. */
	PCGEXPROPERTIESEDITOR_API TSharedPtr<FJsonObject> EncodeRange(const FPCGExNumericRange& Range);
	PCGEXPROPERTIESEDITOR_API bool DecodeRange(const FJsonObject& Json, FPCGExNumericRange& OutRange);
	PCGEXPROPERTIESEDITOR_API TSharedPtr<FJsonObject> DescribeRange();

	/** Enum class by path: native enums resolve directly, user-defined enum assets load. Null (with a diagnostic) when missing. */
	PCGEXPROPERTIESEDITOR_API UEnum* ResolveEnum(const FString& Path);

	/** Class by path, loading a Blueprint class when needed. Null (with a diagnostic) when missing; "" resolves to null silently. */
	PCGEXPROPERTIESEDITOR_API UClass* ResolveClass(const FString& Path);

	/** Any object by path, loading when needed. Null when missing (no diagnostic: callers name what they expected). */
	PCGEXPROPERTIESEDITOR_API UObject* ResolveObject(const FString& Path);

	/** Hooks for the toolkit's own property types. Called by the Properties editor module. */
	PCGEXPROPERTIESEDITOR_API void RegisterBuiltInHooks();
	PCGEXPROPERTIESEDITOR_API void UnregisterBuiltInHooks();
}
