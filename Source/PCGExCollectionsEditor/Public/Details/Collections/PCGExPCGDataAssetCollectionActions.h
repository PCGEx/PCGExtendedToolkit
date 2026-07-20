// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"
#include "Engine/World.h"

#include "PCGExPCGDataAssetCollectionActions.generated.h"

class UPackage;

namespace PCGExPCGDataAssetCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets);
	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExPCGDataAssetCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets);
};

UCLASS()
class UPCGExPCGDataAssetCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExPCGDataAssetCollectionFactory()
	{
		SupportedClass = UPCGExPCGDataAssetCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExPCGDataAssetCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExPCGDataAssetCollection::StaticClass();
	}
};
