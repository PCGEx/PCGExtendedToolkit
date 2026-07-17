// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Elements/Grammar/PCGSubdivisionBase.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "PCGExCollectionToModuleInfos.generated.h"

class UPCGExAssetCollection;

namespace PCGExCollectionToGrammar
{
	struct FModule
	{
		FModule() = default;
		FPCGSubdivisionSubmodule Infos;
		const FPCGExAssetCollectionEntry* Entry = nullptr;
		const UPCGExAssetCollection* Host = nullptr;
		int64 Idx;
	};
}

UCLASS(Hidden, MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="staging/utilities/collection-to-module-infos"))
class UPCGExCollectionToModuleInfosSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExCollectionToModuleInfosElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(CollectionToModuleInfos, "Collection to Module Infos", "Converts an asset collection to a grammar-friendly attribute set that can be used as module infos.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual TOptional<FPCGNodeThumbnailProxy> GetNodeThumbnail() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	/** The mesh collection to read module infos from */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TSoftObjectPtr<UPCGExAssetCollection> AssetCollection;

	/** If enabled, allows duplicate entries (duplicate is same symbol) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bAllowDuplicates = true;

	/** If enabled, skip entries which symbol is "None" */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSkipEmptySymbol = true;

	/** If enabled, invalid or empty entries are removed from the output */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOmitInvalidAndEmpty = true;

	/** Optionally include/exclude entries by category name. Tested against both leaf entries and
	 *  sub-collection containers; excluding a sub-collection skips its descendants entirely. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNameFiltersDetails CategoryFilters;

	/** Name of the attribute the "Symbol" value will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Symbol"))
	FName SymbolAttributeName = FName("Symbol");

	/** Name of the attribute the "Size" value will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Size"))
	FName SizeAttributeName = FName("Size");

	/** Name of the attribute the "Scalable" value will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Scalable"))
	FName ScalableAttributeName = FName("Scalable");

	/** Name of the attribute the "DebugColor" value will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="DebugColor"))
	FName DebugColorAttributeName = FName("DebugColor");

	/** Mesh collection entry Idx. Serialize the id of the parent collection (in the collection map) and entry index within that collection.
	 * This is a critical piece of data that will be used by the Grammar Staging node to retrieve the corresponding mesh. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Entry", EditCondition="false"))
	FName EntryAttributeName = PCGExCollections::Labels::Tag_EntryIdx;

	/** Name of the attribute the entry' Category value will be written to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Category"))
	FName CategoryAttributeName = FName("Category");

	/** Additional custom properties to extract from the collection's CollectionProperties (and per-entry overrides). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(ShowOnlyInnerProperties))
	FPCGExPropertyOutputSettings PropertyOutputSettings;
};

class FPCGExCollectionToModuleInfosElement final : public IPCGExElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;

protected:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	void FlattenCollection(
		const TSharedPtr<PCGExCollections::FPickPacker>& Packer,
		const UPCGExAssetCollection* Collection,
		const UPCGExCollectionToModuleInfosSettings* Settings,
		const FPCGExNameFiltersDetails& CategoryFilters,
		TArray<PCGExCollectionToGrammar::FModule>& OutModules,
		TSet<FName>& OutSymbols,
		FPCGExGrammarSizeCache& SizeCache) const;
};
