// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExLevelCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"
#include "Engine/World.h"

#include "PCGExLevelCollectionActions.generated.h"

class UPackage;

namespace PCGExLevelCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets);
	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExLevelCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets);
};

UCLASS()
class UPCGExLevelCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExLevelCollectionFactory()
	{
		SupportedClass = UPCGExLevelCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExLevelCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExLevelCollection::StaticClass();
	}
};
