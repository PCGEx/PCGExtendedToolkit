// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "JsonObjectStructInterface.h"

/**
 * JSON shapes of the collection structs. An entry is its authored fields by UPROPERTY name (CPF_Edit, not
 * EditConst / deprecated / transient), plus "type" (the registered type id) and "entryId"; object references
 * travel as path strings. One converter instance per registered entry struct, found through the collection type
 * registry, so sibling plugins' types are covered without registration here. Decoding merges into the live
 * entry: an absent key leaves its field unchanged.
 */
class FPCGExCollectionEntryJsonConverter final : public IJsonObjectStructConverter
{
public:
	explicit FPCGExCollectionEntryJsonConverter(const UScriptStruct* InStruct);

	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;

private:
	const UScriptStruct* Struct = nullptr;
};

/** An Omni row: the payload's entry shape; "type" picks (or switches) the payload struct on decode. */
class FPCGExOmniCollectionEntryJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

/** Staging data: authored "sockets" and "boundsModifier" both ways; "path" and "bounds" written for information only. */
class FPCGExAssetStagingDataJsonConverter final : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;
};

namespace PCGExCollectionsMediatorFormats
{
	inline const FName OmniEntryFormatId = FName(TEXT("pcgex.omni-collection-entry"));
	inline const FName StagingFormatId = FName(TEXT("pcgex.asset-staging"));

	/** "pcgex.collection-entry/<TypeId>" */
	FName EntryFormatId(FName TypeId);

	/** Registers the fixed formats and one entry format per registered type; rescans when modules load. */
	void Register();
	void Unregister();
}
