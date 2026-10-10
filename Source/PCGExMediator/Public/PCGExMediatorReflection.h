// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataCommon.h"

class FJsonObject;
class FJsonValue;

/**
 * Reflection in the dialect: one traversal over FProperty that encodes, decodes and describes any reflected value
 * the way the hand-written formats do, so a reflected host reads the same as a curated one.
 *   math structs (Vector2/Vector/Vector4/Rotator/Quat/Transform) -> the dialect shapes
 *   enums (FEnumProperty, byte-with-enum)                         -> enumerator names
 *   object / class refs (not instanced) and soft refs            -> object path strings, "" = null
 *   structs with a registered mediator format                    -> that format's converter
 *   FInstancedStruct, maps, instanced objects                     -> the engine converter (its own shapes)
 *   other structs                                                 -> recursed, by UPROPERTY name
 *   arrays and sets                                               -> arrays
 * Encoding is SPARSE when defaults are supplied: a field identical to its default is omitted. Decoding merges:
 * an absent key leaves its field alone. Failures report at the current diagnostics path.
 */
namespace PCGExMediator::Reflect
{
	/** Which properties a struct walk includes; the default takes every property. */
	using FPropertyFilter = TFunctionRef<bool(const FProperty* Property)>;

	PCGEXMEDIATOR_API bool IncludeAll(const FProperty* Property);

	/** The dialect type a struct maps to, or Unknown. */
	PCGEXMEDIATOR_API EPCGMetadataTypes DialectTypeOf(const UScriptStruct* Struct);

	/** Null when the property kind has no representation (delegates, interfaces, field paths). */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeProperty(const FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr = nullptr);

	PCGEXMEDIATOR_API bool DecodeProperty(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json);

	/** Object of the struct's properties by name; with Defaults (memory of a default instance), fields at default are omitted. */
	PCGEXMEDIATOR_API void EncodeStruct(const UStruct* Struct, const void* Memory, const void* Defaults, FJsonObject& Out, FPropertyFilter Filter = &IncludeAll);

	/**
	 * Applies every key of In to the matching property. Keys in IgnoredKeys are the caller's; other unknown or
	 * filtered-out keys warn and are skipped. False when any value failed (the memory may then be partially
	 * written: decode into a temporary).
	 */
	PCGEXMEDIATOR_API bool DecodeStruct(const UStruct* Struct, void* Memory, const FJsonObject& In, FPropertyFilter Filter = &IncludeAll, TConstArrayView<FString> IgnoredKeys = {});

	/** JSON Schema fragment for one property; Depth bounds the recursion into plain structs. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeProperty(const FProperty* Property, int32 Depth = 3);

	/** Object schema with one entry per included property. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeStruct(const UStruct* Struct, FPropertyFilter Filter = &IncludeAll, int32 Depth = 3);
}
