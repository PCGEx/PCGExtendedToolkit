// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExBoundsEvaluator.h"

#include "PCGExLevelCollection.generated.h"

class UPCGExLevelCollection;

/** Level collection-level globals. Mirrors UPCGExLevelCollection's import members 1:1 -- keep names in sync. */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Level Collection Globals")
struct PCGEXCOLLECTIONS_API FPCGExLevelCollectionGlobals : public FPCGExCollectionTypeGlobals
{
	GENERATED_BODY()

	/** Actor content filter for bounds computation. If null, default infrastructure checks are used. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExActorContentFilter> ContentFilter;

	/** Bounds evaluator for bounds computation. If null, bounds default to empty. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;
};

/**
 * Level collection entry. References a UWorld level asset or, via the base SubCollection
 * property, any collection type as a subcollection. UpdateStaging() loads the level
 * package in-editor to compute combined bounds from spatial actors.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Level Collection Entry", meta=(ShortName="Level"))
struct PCGEXCOLLECTIONS_API FPCGExLevelCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExLevelCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Level;
	}

	// Level-Specific Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> Level = nullptr;

	// Lifecycle
	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;

#if WITH_EDITOR
	virtual void EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
#endif
};

/** Concrete collection for level/world assets. */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Level", meta=(ToolTip = "A weighted collection of level assets."))
class PCGEXCOLLECTIONS_API UPCGExLevelCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExLevelCollectionEntry)

public:
	UPCGExLevelCollection(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	friend struct FPCGExLevelCollectionEntry;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Level;
	}

	/** Actor content filter for bounds computation. If null, default infrastructure checks are used. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Settings|Import")
	TObjectPtr<UPCGExActorContentFilter> ContentFilter;

	/** Bounds evaluator for bounds computation. If null, bounds default to empty. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Settings|Import")
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExLevelCollectionEntry> Entries;

protected:
	virtual bool GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const override;

public:
#if WITH_EDITOR
	// Editor Functions
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
