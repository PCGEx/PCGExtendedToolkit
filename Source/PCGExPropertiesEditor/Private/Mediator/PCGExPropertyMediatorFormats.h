// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMediatorConverter.h"
#include "PCGExEnumSelector.h"
#include "PCGExPropertySchema.h"
#include "PCGExPropertyTypes.h"

class FPCGExMediatorDomain;

/**
 * JSON shapes of the Properties structs, registered with the mediator (and through it, the engine's struct
 * converter registry) so they apply wherever the structs appear. Standalone classes: the runtime structs gain
 * nothing. The converter base decodes into a temporary and assigns only on success.
 */

/** { "properties": [ { name, type, default?, enum? | allowedClass? | range? } ], "imports"?: [paths], "importOverrides"?: {name: value} } */
class FPCGExPropertySchemaCollectionJsonConverter final : public TPCGExMediatorStructConverter<FPCGExPropertySchemaCollection>
{
protected:
	virtual bool Encode(const FPCGExPropertySchemaCollection& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExPropertySchemaCollection& Temp, const FPCGExPropertySchemaCollection& Live) const override;
};

/** { name: value } of ENABLED entries; the document is the full enabled set. See PCGExPropertyMediatorFormats::DecodeOverridesBody. */
class FPCGExPropertyOverridesJsonConverter final : public TPCGExMediatorStructConverter<FPCGExPropertyOverrides>
{
protected:
	virtual bool Encode(const FPCGExPropertyOverrides& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExPropertyOverrides& Temp, const FPCGExPropertyOverrides& Live) const override;
};

/** { "weight": N, "values": { name: value } }: an override set with a distribution weight (Distribute Tuple rows). */
class FPCGExWeightedPropertyOverridesJsonConverter final : public TPCGExMediatorStructConverter<FPCGExWeightedPropertyOverrides>
{
protected:
	virtual bool Encode(const FPCGExWeightedPropertyOverrides& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExWeightedPropertyOverrides& Temp, const FPCGExWeightedPropertyOverrides& Live) const override;
};

/** { "enum"?: class path, "value": enumerator } */
class FPCGExEnumSelectorJsonConverter final : public TPCGExMediatorStructConverter<FPCGExEnumSelector>
{
protected:
	virtual bool Encode(const FPCGExEnumSelector& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExEnumSelector& Temp, const FPCGExEnumSelector& Live) const override;
};

/** { "min", "max", "clampMin", "clampMax" } */
class FPCGExNumericRangeJsonConverter final : public TPCGExMediatorStructConverter<FPCGExNumericRange>
{
protected:
	virtual bool Encode(const FPCGExNumericRange& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExNumericRange& Temp, const FPCGExNumericRange& Live) const override;
};

namespace PCGExPropertyMediatorFormats
{
	inline const FName SchemaFormatId = FName(TEXT("pcgex.property-schema"));
	inline const FName OverridesFormatId = FName(TEXT("pcgex.property-overrides"));
	inline const FName WeightedOverridesFormatId = FName(TEXT("pcgex.weighted-property-overrides"));
	inline const FName EnumSelectorFormatId = FName(TEXT("pcgex.enum-selector"));
	inline const FName NumericRangeFormatId = FName(TEXT("pcgex.numeric-range"));

	/** Registers the five formats on the module's domain. */
	void Register(FPCGExMediatorDomain& Domain);
}
