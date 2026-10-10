// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertySchema.h"

struct FInstancedStruct;

/** One concrete FPCGExProperty type, as the JSON side names and instantiates it. */
struct PCGEXPROPERTIESEDITOR_API FPCGExPropertyTypeInfo
{
	/** The type's registry entry (TypeName is the "type" a document uses; PropertyName unused). */
	FPCGExPropertyRegistryEntry Entry;

	const UScriptStruct* Struct = nullptr;

	/** USTRUCT DisplayName, for listings. */
	FString DisplayName;
};

/**
 * Every FPCGExProperty subclass the reflection system knows, found by iterating script structs and instantiated
 * once to read its name and capabilities. Rebuilt lazily after any module loads or unloads, so types from
 * sibling plugins appear without registration. Game thread only.
 */
namespace PCGExPropertyCatalog
{
	/** Sorted by TypeName. */
	PCGEXPROPERTIESEDITOR_API const TArray<FPCGExPropertyTypeInfo>& Get();

	/** By type name, or by struct path for a type whose name collides with another. */
	PCGEXPROPERTIESEDITOR_API const FPCGExPropertyTypeInfo* Find(const FString& TypeNameOrStructPath);

	PCGEXPROPERTIESEDITOR_API const FPCGExPropertyTypeInfo* FindByStruct(const UScriptStruct* Struct);

	/** Initializes OutProperty as Info's type, named PropertyName, HeaderId left at 0 for the schema sync to mint. */
	PCGEXPROPERTIESEDITOR_API bool MakeProperty(const FPCGExPropertyTypeInfo& Info, FName PropertyName, FInstancedStruct& OutProperty);

	/** Comma-separated type names, for diagnostics. */
	PCGEXPROPERTIESEDITOR_API FString ListTypeNames();

	PCGEXPROPERTIESEDITOR_API void Invalidate();

	/** Module hooks: subscribe to / release the module-change invalidation. */
	PCGEXPROPERTIESEDITOR_API void Startup();
	PCGEXPROPERTIESEDITOR_API void Shutdown();
}
