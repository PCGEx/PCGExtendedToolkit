// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExSkinnedMeshCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"
#include "Engine/World.h"

#include "PCGExSkinnedMeshCollectionActions.generated.h"

class UPackage;

UCLASS()
class UPCGExSkinnedMeshCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExSkinnedMeshCollectionFactory()
	{
		SupportedClass = UPCGExSkinnedMeshCollection::StaticClass();
		bHiddenFromNewMenu = true;
	}
};

UCLASS()
class UAssetDefinition_PCGExSkinnedMeshCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExSkinnedMeshCollection::StaticClass();
	}
};
