// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "JsonObjectStructInterface.h"

/**
 * JSON shapes of the Properties structs, registered with the mediator (and through it, the engine's struct
 * converter registry) so they apply wherever the structs appear. Standalone classes: the runtime structs gain
 * nothing. Every ConvertFromJson decodes into a temporary and assigns only on success.
 */

/** { "properties": [ { name, type, default?, enum? | allowedClass? | range? } ], "imports"?: [paths], "importOverrides"?: {name: value} } */
class FPCGExPropertySchemaCollectionJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

/** { name: value } of ENABLED entries; the document is the full enabled set. See PCGExPropertyMediatorFormats::DecodeOverridesBody. */
class FPCGExPropertyOverridesJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

/** { "enum"?: class path, "value": enumerator } */
class FPCGExEnumSelectorJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

/** { "min", "max", "clampMin", "clampMax" } */
class FPCGExNumericRangeJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

namespace PCGExPropertyMediatorFormats
{
	inline const FName SchemaFormatId = FName(TEXT("pcgex.property-schema"));
	inline const FName OverridesFormatId = FName(TEXT("pcgex.property-overrides"));
	inline const FName EnumSelectorFormatId = FName(TEXT("pcgex.enum-selector"));
	inline const FName NumericRangeFormatId = FName(TEXT("pcgex.numeric-range"));

	/** Registers the four formats with the mediator. Called by the Properties editor module. */
	void Register();
	void Unregister();
}
