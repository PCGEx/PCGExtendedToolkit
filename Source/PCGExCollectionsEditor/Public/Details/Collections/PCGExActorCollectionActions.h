// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExActorCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"
#include "Engine/World.h"

#include "PCGExActorCollectionActions.generated.h"

class UPackage;

UCLASS()
class UPCGExActorCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExActorCollectionFactory()
	{
		SupportedClass = UPCGExActorCollection::StaticClass();
		bHiddenFromNewMenu = true;
	}
};

UCLASS()
class UAssetDefinition_PCGExActorCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExActorCollection::StaticClass();
	}
};
