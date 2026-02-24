// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "PCGExPCGDataAssetCollection.generated.h"

class UPCGDataAsset;
class UPCGExPCGDataAssetCollection;
class UPCGExLevelDataExporter;
class UPCGExMeshCollection;
class UPCGExActorCollection;
class UWorld;

UENUM()
enum class EPCGExDataAssetEntrySource : uint8
{
	DataAsset = 0 UMETA(DisplayName = "Data Asset", ToolTip="Reference an existing PCGDataAsset", ActionIcon="PCGDA_DataAsset"),
	Level     = 1 UMETA(DisplayName = "Level",      ToolTip="Export a level to an embedded PCGDataAsset", ActionIcon="PCGDA_Level"),
};

/**
 * PCG data asset collection entry. References a UPCGDataAsset or a subcollection.
 * UpdateStaging() computes combined bounds from all spatial data in the asset.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] PCGDataAsset Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExPCGDataAssetCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExPCGDataAssetCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::PCGDataAsset;
	}

	// PCGDataAsset-Specific Properties

	/** Source mode toggle (default = DataAsset for backward compatibility) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExDataAssetEntrySource Source = EPCGExDataAssetEntrySource::DataAsset;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="Source==EPCGExDataAssetEntrySource::DataAsset && !bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UPCGDataAsset> DataAsset = nullptr;

	/** Level reference (used when Source == Level) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="Source==EPCGExDataAssetEntrySource::Level && !bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> Level;

	/** Embedded exported data asset (hidden, serialized, outered to collection) */
	UPROPERTY()
	TObjectPtr<UPCGDataAsset> ExportedDataAsset;

	/** Embedded mesh collection built by level exporter when bGenerateCollections is enabled */
	UPROPERTY()
	TObjectPtr<UPCGExMeshCollection> EmbeddedMeshCollection;

	/** Embedded actor collection built by level exporter when bGenerateCollections is enabled */
	UPROPERTY()
	TObjectPtr<UPCGExActorCollection> EmbeddedActorCollection;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection", EditConditionHides, DisplayAfter="bIsSubCollection"))
	TObjectPtr<UPCGExPCGDataAssetCollection> SubCollection;

	// Subcollection Access

	virtual UPCGExAssetCollection* GetSubCollectionPtr() const override;

	virtual void ClearSubCollection() override;

	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
#endif

};

/** Concrete collection for UPCGDataAsset references with optional level-sourced entries. */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | PCGDataAsset")
class PCGEXCOLLECTIONS_API UPCGExPCGDataAssetCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExPCGDataAssetCollectionEntry)

public:
	friend struct FPCGExPCGDataAssetCollectionEntry;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::PCGDataAsset;
	}

	// Settings

	/** Exporter used to convert level-sourced entries into embedded PCGDataAssets during staging.
	 *  If unset, a default exporter is used. Instanced so custom exporters can expose their own settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Settings)
	TObjectPtr<UPCGExLevelDataExporter> LevelExporter;

	// Entries Array

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExPCGDataAssetCollectionEntry> Entries;

	// Editor Functions

#if WITH_EDITOR
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
