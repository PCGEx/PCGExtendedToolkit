// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExDataAssetFactory.h"
#include "Collections/PCGExOmniCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"

#include "PCGExOmniCollectionActions.generated.h"

UCLASS()
class UPCGExOmniCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExOmniCollectionFactory()
	{
		SupportedClass = UPCGExOmniCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExOmniCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExOmniCollection::StaticClass();
	}
};
