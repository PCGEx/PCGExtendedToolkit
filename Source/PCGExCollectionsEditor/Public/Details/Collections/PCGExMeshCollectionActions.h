// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExMeshCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"
#include "Engine/World.h"

#include "PCGExMeshCollectionActions.generated.h"

class UPackage;

namespace PCGExMeshCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets);
	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExMeshCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets);
};

UCLASS()
class UPCGExMeshCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExMeshCollectionFactory()
	{
		SupportedClass = UPCGExMeshCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExMeshCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExMeshCollection::StaticClass();
	}
};
